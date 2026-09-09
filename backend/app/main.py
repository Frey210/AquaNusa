import hmac
import base64
import hashlib
import os
import secrets
from contextlib import asynccontextmanager
from datetime import datetime, timedelta, timezone
from typing import Annotated

from fastapi import Cookie, Depends, FastAPI, Header, HTTPException, Query, Response
from fastapi.middleware.cors import CORSMiddleware
from dotenv import load_dotenv
from pydantic import BaseModel, ConfigDict, Field
from sqlalchemy import DateTime, Float, ForeignKey, String, create_engine, desc, select
from sqlalchemy.orm import DeclarativeBase, Mapped, Session, mapped_column, relationship, sessionmaker


load_dotenv()
DATABASE_URL = os.getenv("DATABASE_URL", "sqlite:///./aquanusa.db")
DEVICE_KEY = os.getenv("AQUANUSA_DEVICE_KEY", "change-me")
SESSION_COOKIE = "aquanusa_session"
SESSION_HOURS = 24 * 30
COOKIE_SECURE = os.getenv("COOKIE_SECURE", "true").lower() == "true"
INITIAL_DEVICE_UID = os.getenv("INITIAL_DEVICE_UID", "AQUANUSA-001")
INITIAL_USER_EMAIL = os.getenv("AQUANUSA_USER_EMAIL", "").lower()
engine = create_engine(DATABASE_URL, connect_args={"check_same_thread": False} if DATABASE_URL.startswith("sqlite") else {})
SessionLocal = sessionmaker(bind=engine, expire_on_commit=False)


class Base(DeclarativeBase):
    pass


class Device(Base):
    __tablename__ = "devices"
    uid: Mapped[str] = mapped_column(String(64), primary_key=True)
    name: Mapped[str | None] = mapped_column(String(100))
    last_seen: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))
    readings: Mapped[list["Reading"]] = relationship(back_populates="device", cascade="all, delete-orphan")


class User(Base):
    __tablename__ = "users"
    id: Mapped[int] = mapped_column(primary_key=True)
    name: Mapped[str] = mapped_column(String(100))
    email: Mapped[str] = mapped_column(String(254), unique=True, index=True)
    password_hash: Mapped[str] = mapped_column(String(255))
    role: Mapped[str] = mapped_column(String(16), default="user")


class AuthSession(Base):
    __tablename__ = "auth_sessions"
    token_hash: Mapped[str] = mapped_column(String(64), primary_key=True)
    user_id: Mapped[int] = mapped_column(ForeignKey("users.id"), index=True)
    expires_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), index=True)


class DeviceAccess(Base):
    __tablename__ = "device_access"
    device_uid: Mapped[str] = mapped_column(ForeignKey("devices.uid"), primary_key=True)
    user_id: Mapped[int] = mapped_column(ForeignKey("users.id"), primary_key=True)


class Reading(Base):
    __tablename__ = "readings"
    id: Mapped[int] = mapped_column(primary_key=True)
    device_uid: Mapped[str] = mapped_column(ForeignKey("devices.uid"), index=True)
    recorded_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), index=True)
    water_temp_c: Mapped[float] = mapped_column(Float)
    air_temp_c: Mapped[float] = mapped_column(Float)
    do_mg_l: Mapped[float] = mapped_column(Float)
    ph: Mapped[float] = mapped_column(Float)
    humidity_rh: Mapped[float] = mapped_column(Float)
    illuminance_lux: Mapped[float] = mapped_column(Float)
    device: Mapped[Device] = relationship(back_populates="readings")


class TelemetryIn(BaseModel):
    uid: str = Field(min_length=3, max_length=64, pattern=r"^[A-Za-z0-9._-]+$")
    water_temp_c: float = Field(ge=-10, le=60)
    air_temp_c: float = Field(ge=-40, le=85)
    do_mg_l: float = Field(ge=0, le=30)
    ph: float = Field(ge=0, le=14)
    humidity_rh: float = Field(ge=0, le=100)
    illuminance_lux: float = Field(ge=0, le=200_000)


class ReadingOut(TelemetryIn):
    id: int
    recorded_at: datetime
    model_config = ConfigDict(from_attributes=True)


class DeviceOut(BaseModel):
    uid: str
    name: str | None
    last_seen: datetime | None
    online: bool
    latest: ReadingOut | None


class LoginIn(BaseModel):
    email: str = Field(min_length=3, max_length=254)
    password: str = Field(min_length=8, max_length=128)


class UserOut(BaseModel):
    id: int
    name: str
    email: str
    role: str
    model_config = ConfigDict(from_attributes=True)


def get_db():
    with SessionLocal() as db:
        yield db


def require_device_key(x_device_key: Annotated[str | None, Header()] = None):
    if not x_device_key or not hmac.compare_digest(x_device_key, DEVICE_KEY):
        raise HTTPException(401, "Invalid device key")


def hash_password(password: str) -> str:
    salt = secrets.token_bytes(16)
    digest = hashlib.scrypt(password.encode(), salt=salt, n=2**14, r=8, p=1)
    return f"{base64.urlsafe_b64encode(salt).decode()}${base64.urlsafe_b64encode(digest).decode()}"


def verify_password(password: str, encoded: str) -> bool:
    try:
        salt_text, digest_text = encoded.split("$", 1)
        salt = base64.urlsafe_b64decode(salt_text)
        expected = base64.urlsafe_b64decode(digest_text)
        actual = hashlib.scrypt(password.encode(), salt=salt, n=2**14, r=8, p=1)
        return hmac.compare_digest(actual, expected)
    except (ValueError, TypeError):
        return False


def token_hash(token: str) -> str:
    return hashlib.sha256(token.encode()).hexdigest()


def get_current_user(session_token: Annotated[str | None, Cookie(alias=SESSION_COOKIE)] = None,
                     db: Session = Depends(get_db)) -> User:
    if not session_token:
        raise HTTPException(401, "Authentication required")
    session = db.get(AuthSession, token_hash(session_token))
    if not session:
        raise HTTPException(401, "Session invalid")
    expires_at = session.expires_at
    if expires_at.tzinfo is None:
        expires_at = expires_at.replace(tzinfo=timezone.utc)
    if expires_at <= datetime.now(timezone.utc):
        db.delete(session)
        db.commit()
        raise HTTPException(401, "Session expired")
    user = db.get(User, session.user_id)
    if not user:
        raise HTTPException(401, "User no longer exists")
    return user


def can_access_device(db: Session, user: User, uid: str) -> bool:
    return user.role == "admin" or db.get(DeviceAccess, (uid, user.id)) is not None


def seed_account(db: Session, prefix: str, role: str) -> User | None:
    email = os.getenv(f"AQUANUSA_{prefix}_EMAIL", "").strip().lower()
    password = os.getenv(f"AQUANUSA_{prefix}_PASSWORD", "")
    name = os.getenv(f"AQUANUSA_{prefix}_NAME", prefix.title()).strip()
    if not email or not password:
        return None
    user = db.scalar(select(User).where(User.email == email))
    if not user:
        user = User(name=name, email=email, password_hash=hash_password(password), role=role)
        db.add(user)
        db.flush()
    else:
        user.name, user.role = name, role
    return user


def assign_initial_device(db: Session, user: User | None):
    if user and db.get(Device, INITIAL_DEVICE_UID) and not db.get(DeviceAccess, (INITIAL_DEVICE_UID, user.id)):
        db.add(DeviceAccess(device_uid=INITIAL_DEVICE_UID, user_id=user.id))


def reading_out(uid: str, reading: Reading) -> ReadingOut:
    recorded_at = reading.recorded_at
    if recorded_at.tzinfo is None:
        recorded_at = recorded_at.replace(tzinfo=timezone.utc)
    return ReadingOut(uid=uid, **{**reading.__dict__, "recorded_at": recorded_at})


@asynccontextmanager
async def lifespan(_: FastAPI):
    Base.metadata.create_all(engine)
    with SessionLocal() as db:
        seed_account(db, "ADMIN", "admin")
        user = seed_account(db, "USER", "user")
        assign_initial_device(db, user)
        db.commit()
    yield


app = FastAPI(title="AquaNusa API", version="0.1.0", lifespan=lifespan)
origins = [value.strip() for value in os.getenv("CORS_ORIGINS", "http://localhost:5173").split(",") if value.strip()]
app.add_middleware(CORSMiddleware, allow_origins=origins, allow_credentials=True, allow_methods=["GET", "POST"], allow_headers=["Content-Type", "X-Device-Key"])


@app.get("/health")
def health():
    return {"status": "ok"}


@app.post("/api/v1/telemetry", response_model=ReadingOut, status_code=201, dependencies=[Depends(require_device_key)])
def create_telemetry(payload: TelemetryIn, db: Annotated[Session, Depends(get_db)]):
    now = datetime.now(timezone.utc)
    device = db.get(Device, payload.uid)
    if not device:
        device = Device(uid=payload.uid, name=payload.uid)
        db.add(device)
        db.flush()
    if payload.uid == INITIAL_DEVICE_UID:
        assign_initial_device(db, db.scalar(select(User).where(User.email == INITIAL_USER_EMAIL)))
    device.last_seen = now
    reading = Reading(**payload.model_dump(exclude={"uid"}), device_uid=payload.uid, recorded_at=now)
    db.add(reading)
    db.commit()
    db.refresh(reading)
    return reading_out(payload.uid, reading)


@app.post("/api/v1/auth/login", response_model=UserOut)
def login(payload: LoginIn, response: Response, db: Annotated[Session, Depends(get_db)]):
    user = db.scalar(select(User).where(User.email == payload.email.strip().lower()))
    if not user or not verify_password(payload.password, user.password_hash):
        raise HTTPException(401, "Email atau password salah")
    raw_token = secrets.token_urlsafe(32)
    db.add(AuthSession(token_hash=token_hash(raw_token), user_id=user.id,
                       expires_at=datetime.now(timezone.utc) + timedelta(hours=SESSION_HOURS)))
    db.commit()
    response.set_cookie(SESSION_COOKIE, raw_token, max_age=SESSION_HOURS * 3600,
                        httponly=True, secure=COOKIE_SECURE, samesite="strict", path="/")
    return user


@app.get("/api/v1/auth/me", response_model=UserOut)
def auth_me(user: Annotated[User, Depends(get_current_user)]):
    return user


@app.post("/api/v1/auth/logout", status_code=204)
def logout(response: Response, db: Annotated[Session, Depends(get_db)],
           session_token: Annotated[str | None, Cookie(alias=SESSION_COOKIE)] = None):
    if session_token:
        session = db.get(AuthSession, token_hash(session_token))
        if session:
            db.delete(session)
            db.commit()
    response.delete_cookie(SESSION_COOKIE, path="/", secure=COOKIE_SECURE, samesite="strict")


@app.get("/api/v1/devices", response_model=list[DeviceOut])
def list_devices(db: Annotated[Session, Depends(get_db)], user: Annotated[User, Depends(get_current_user)]):
    query = select(Device).order_by(Device.name)
    if user.role != "admin":
        query = query.join(DeviceAccess).where(DeviceAccess.user_id == user.id)
    devices = db.scalars(query).all()
    cutoff = datetime.now(timezone.utc) - timedelta(minutes=5)
    result = []
    for device in devices:
        latest = db.scalar(select(Reading).where(Reading.device_uid == device.uid).order_by(desc(Reading.recorded_at)).limit(1))
        seen = device.last_seen
        if seen and seen.tzinfo is None:
            seen = seen.replace(tzinfo=timezone.utc)
        result.append(DeviceOut(uid=device.uid, name=device.name, last_seen=seen, online=bool(seen and seen >= cutoff), latest=reading_out(device.uid, latest) if latest else None))
    return result


@app.get("/api/v1/devices/{uid}/history", response_model=list[ReadingOut])
def device_history(uid: str, db: Annotated[Session, Depends(get_db)], user: Annotated[User, Depends(get_current_user)], hours: Annotated[int, Query(ge=1, le=168)] = 24, limit: Annotated[int, Query(ge=1, le=5000)] = 500):
    if not db.get(Device, uid):
        raise HTTPException(404, "Device not found")
    if not can_access_device(db, user, uid):
        raise HTTPException(403, "Device access denied")
    readings = db.scalars(select(Reading).where(Reading.device_uid == uid, Reading.recorded_at >= datetime.now(timezone.utc) - timedelta(hours=hours)).order_by(Reading.recorded_at).limit(limit)).all()
    return [reading_out(uid, reading) for reading in readings]
