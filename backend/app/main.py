import hmac
import base64
import csv
import hashlib
import io
import logging
import os
import secrets
from contextlib import asynccontextmanager
from datetime import datetime, timedelta, timezone
from typing import Annotated

from fastapi import BackgroundTasks, Cookie, Depends, FastAPI, Header, HTTPException, Query, Response
from fastapi.middleware.cors import CORSMiddleware
from dotenv import load_dotenv
from pydantic import BaseModel, ConfigDict, Field, model_validator
from sqlalchemy import Boolean, DateTime, Float, ForeignKey, String, create_engine, desc, select
from sqlalchemy.orm import DeclarativeBase, Mapped, Session, mapped_column, relationship, sessionmaker


load_dotenv()
DATABASE_URL = os.getenv("DATABASE_URL", "sqlite:///./aquanusa.db")
DEVICE_KEY = os.getenv("AQUANUSA_DEVICE_KEY", "change-me")
SESSION_COOKIE = "aquanusa_session"
SESSION_HOURS = 24 * 30
COOKIE_SECURE = os.getenv("COOKIE_SECURE", "true").lower() == "true"
INITIAL_DEVICE_UID = os.getenv("INITIAL_DEVICE_UID", "AQUANUSA-001")
INITIAL_USER_EMAIL = os.getenv("AQUANUSA_USER_EMAIL", "").lower()
FIREBASE_CREDENTIALS = os.getenv("FIREBASE_CREDENTIALS", "")
logger = logging.getLogger("aquanusa")
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


class PushToken(Base):
    __tablename__ = "push_tokens"
    user_id: Mapped[int] = mapped_column(ForeignKey("users.id"), primary_key=True)
    token: Mapped[str] = mapped_column(String(4096), unique=True)


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


class Threshold(Base):
    __tablename__ = "thresholds"
    device_uid: Mapped[str] = mapped_column(ForeignKey("devices.uid"), primary_key=True)
    water_temp_c_min: Mapped[float] = mapped_column(Float, default=20)
    water_temp_c_max: Mapped[float] = mapped_column(Float, default=32)
    air_temp_c_min: Mapped[float] = mapped_column(Float, default=18)
    air_temp_c_max: Mapped[float] = mapped_column(Float, default=38)
    do_mg_l_min: Mapped[float] = mapped_column(Float, default=5)
    do_mg_l_max: Mapped[float] = mapped_column(Float, default=12)
    ph_min: Mapped[float] = mapped_column(Float, default=6.5)
    ph_max: Mapped[float] = mapped_column(Float, default=8.5)
    humidity_rh_min: Mapped[float] = mapped_column(Float, default=30)
    humidity_rh_max: Mapped[float] = mapped_column(Float, default=95)
    illuminance_lux_min: Mapped[float] = mapped_column(Float, default=0)
    illuminance_lux_max: Mapped[float] = mapped_column(Float, default=200_000)


class Notification(Base):
    __tablename__ = "notifications"
    id: Mapped[int] = mapped_column(primary_key=True)
    device_uid: Mapped[str] = mapped_column(ForeignKey("devices.uid"), index=True)
    parameter: Mapped[str] = mapped_column(String(32))
    direction: Mapped[str] = mapped_column(String(8))
    value: Mapped[float] = mapped_column(Float)
    threshold: Mapped[float] = mapped_column(Float)
    message: Mapped[str] = mapped_column(String(255))
    read: Mapped[bool] = mapped_column(Boolean, default=False)
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), index=True)


class AlertState(Base):
    __tablename__ = "alert_states"
    device_uid: Mapped[str] = mapped_column(ForeignKey("devices.uid"), primary_key=True)
    parameter: Mapped[str] = mapped_column(String(32), primary_key=True)
    direction: Mapped[str] = mapped_column(String(8))


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


class ThresholdIn(BaseModel):
    water_temp_c_min: float = Field(ge=-10, le=60)
    water_temp_c_max: float = Field(ge=-10, le=60)
    air_temp_c_min: float = Field(ge=-40, le=85)
    air_temp_c_max: float = Field(ge=-40, le=85)
    do_mg_l_min: float = Field(ge=0, le=30)
    do_mg_l_max: float = Field(ge=0, le=30)
    ph_min: float = Field(ge=0, le=14)
    ph_max: float = Field(ge=0, le=14)
    humidity_rh_min: float = Field(ge=0, le=100)
    humidity_rh_max: float = Field(ge=0, le=100)
    illuminance_lux_min: float = Field(ge=0, le=200_000)
    illuminance_lux_max: float = Field(ge=0, le=200_000)

    @model_validator(mode="after")
    def minimums_must_be_lower(self):
        for key in SENSOR_LABELS:
            if getattr(self, f"{key}_min") >= getattr(self, f"{key}_max"):
                raise ValueError(f"Nilai minimum {SENSOR_LABELS[key]} harus lebih kecil dari maksimum")
        return self


class ThresholdOut(ThresholdIn):
    device_uid: str
    model_config = ConfigDict(from_attributes=True)


class NotificationOut(BaseModel):
    id: int
    device_uid: str
    parameter: str
    direction: str
    value: float
    threshold: float
    message: str
    read: bool
    created_at: datetime
    model_config = ConfigDict(from_attributes=True)


class PushTokenIn(BaseModel):
    token: str = Field(min_length=10, max_length=4096)


SENSOR_LABELS = {
    "water_temp_c": "suhu air", "air_temp_c": "suhu udara", "do_mg_l": "DO",
    "ph": "pH", "humidity_rh": "kelembapan", "illuminance_lux": "illuminance",
}


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
        if not verify_password(password, user.password_hash):
            user.password_hash = hash_password(password)
            for session in db.scalars(select(AuthSession).where(AuthSession.user_id == user.id)):
                db.delete(session)
    return user


def get_threshold(db: Session, uid: str) -> Threshold:
    threshold = db.get(Threshold, uid)
    if not threshold:
        threshold = Threshold(device_uid=uid)
        db.add(threshold)
        db.flush()
    return threshold


def send_push(token: str, title: str, body: str, data: dict[str, str]):
    if not FIREBASE_CREDENTIALS or not os.path.isfile(FIREBASE_CREDENTIALS):
        return
    try:
        import firebase_admin
        from firebase_admin import credentials, messaging
        if not firebase_admin._apps:
            firebase_admin.initialize_app(credentials.Certificate(FIREBASE_CREDENTIALS))
        messaging.send(messaging.Message(notification=messaging.Notification(title=title, body=body), data=data, token=token))
    except Exception as error:
        logger.warning("Push notification failed: %s", error)


def evaluate_alerts(db: Session, payload: TelemetryIn, now: datetime, background_tasks: BackgroundTasks):
    threshold = get_threshold(db, payload.uid)
    for parameter, label in SENSOR_LABELS.items():
        value = getattr(payload, parameter)
        minimum, maximum = getattr(threshold, f"{parameter}_min"), getattr(threshold, f"{parameter}_max")
        direction = "low" if value < minimum else "high" if value > maximum else None
        state = db.get(AlertState, (payload.uid, parameter))
        if not direction:
            if state:
                db.delete(state)
            continue
        limit = minimum if direction == "low" else maximum
        if not state or state.direction != direction:
            message = f"{label} {value:g} melewati batas {'minimum' if direction == 'low' else 'maksimum'} {limit:g}"
            db.add(Notification(device_uid=payload.uid, parameter=parameter, direction=direction,
                                value=value, threshold=limit,
                                message=message,
                                created_at=now))
            for push_token in db.scalars(select(PushToken).join(User).where((User.role == "admin") | User.id.in_(select(DeviceAccess.user_id).where(DeviceAccess.device_uid == payload.uid)))):
                background_tasks.add_task(send_push, push_token.token, f"Peringatan {payload.uid}", message, {"device_uid": payload.uid, "parameter": parameter})
            if state:
                state.direction = direction
            else:
                db.add(AlertState(device_uid=payload.uid, parameter=parameter, direction=direction))


def assign_initial_device(db: Session, user: User | None):
    if user and db.get(Device, INITIAL_DEVICE_UID) and not db.get(DeviceAccess, (INITIAL_DEVICE_UID, user.id)):
        db.add(DeviceAccess(device_uid=INITIAL_DEVICE_UID, user_id=user.id))


def reading_out(uid: str, reading: Reading) -> ReadingOut:
    recorded_at = reading.recorded_at
    if recorded_at.tzinfo is None:
        recorded_at = recorded_at.replace(tzinfo=timezone.utc)
    return ReadingOut(uid=uid, **{**reading.__dict__, "recorded_at": recorded_at})


def notification_out(notification: Notification) -> NotificationOut:
    created_at = notification.created_at
    if created_at.tzinfo is None:
        created_at = created_at.replace(tzinfo=timezone.utc)
    return NotificationOut(**{**notification.__dict__, "created_at": created_at})


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
app.add_middleware(CORSMiddleware, allow_origins=origins, allow_credentials=True, allow_methods=["GET", "POST", "PUT"], allow_headers=["Content-Type", "X-Device-Key"])


@app.get("/health")
def health():
    return {"status": "ok"}


@app.post("/api/v1/telemetry", response_model=ReadingOut, status_code=201, dependencies=[Depends(require_device_key)])
def create_telemetry(payload: TelemetryIn, background_tasks: BackgroundTasks, db: Annotated[Session, Depends(get_db)]):
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
    evaluate_alerts(db, payload, now, background_tasks)
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


def require_device_access(db: Session, user: User, uid: str):
    if not db.get(Device, uid):
        raise HTTPException(404, "Device not found")
    if not can_access_device(db, user, uid):
        raise HTTPException(403, "Device access denied")


@app.get("/api/v1/devices/{uid}/thresholds", response_model=ThresholdOut)
def device_thresholds(uid: str, db: Annotated[Session, Depends(get_db)], user: Annotated[User, Depends(get_current_user)]):
    require_device_access(db, user, uid)
    threshold = get_threshold(db, uid)
    db.commit()
    return threshold


@app.put("/api/v1/devices/{uid}/thresholds", response_model=ThresholdOut)
def update_device_thresholds(uid: str, payload: ThresholdIn, db: Annotated[Session, Depends(get_db)], user: Annotated[User, Depends(get_current_user)]):
    require_device_access(db, user, uid)
    threshold = get_threshold(db, uid)
    for key, value in payload.model_dump().items():
        setattr(threshold, key, value)
    db.commit()
    db.refresh(threshold)
    return threshold


@app.get("/api/v1/devices/{uid}/export.csv")
def export_device_history(uid: str, db: Annotated[Session, Depends(get_db)], user: Annotated[User, Depends(get_current_user)], hours: Annotated[int, Query(ge=1, le=168)] = 24):
    require_device_access(db, user, uid)
    rows = db.scalars(select(Reading).where(Reading.device_uid == uid, Reading.recorded_at >= datetime.now(timezone.utc) - timedelta(hours=hours)).order_by(Reading.recorded_at)).all()
    output = io.StringIO()
    writer = csv.writer(output)
    writer.writerow(["recorded_at", *SENSOR_LABELS])
    for row in rows:
        recorded_at = row.recorded_at if row.recorded_at.tzinfo else row.recorded_at.replace(tzinfo=timezone.utc)
        writer.writerow([recorded_at.isoformat(), *(getattr(row, key) for key in SENSOR_LABELS)])
    filename = f"aquanusa-{uid}-{datetime.now(timezone.utc).date()}.csv"
    return Response(output.getvalue(), media_type="text/csv; charset=utf-8", headers={"Content-Disposition": f'attachment; filename="{filename}"'})


@app.get("/api/v1/notifications", response_model=list[NotificationOut])
def list_notifications(db: Annotated[Session, Depends(get_db)], user: Annotated[User, Depends(get_current_user)], limit: Annotated[int, Query(ge=1, le=1000)] = 200):
    query = select(Notification).order_by(desc(Notification.created_at)).limit(limit)
    if user.role != "admin":
        query = query.join(DeviceAccess, DeviceAccess.device_uid == Notification.device_uid).where(DeviceAccess.user_id == user.id)
    return [notification_out(notification) for notification in db.scalars(query).all()]


@app.put("/api/v1/notifications/push-token", status_code=204)
def update_push_token(payload: PushTokenIn, db: Annotated[Session, Depends(get_db)], user: Annotated[User, Depends(get_current_user)]):
    duplicate = db.scalar(select(PushToken).where(PushToken.token == payload.token, PushToken.user_id != user.id))
    if duplicate:
        db.delete(duplicate)
    token = db.get(PushToken, user.id)
    if token:
        token.token = payload.token
    else:
        db.add(PushToken(user_id=user.id, token=payload.token))
    db.commit()


@app.post("/api/v1/notifications/read-all", status_code=204)
def read_all_notifications(db: Annotated[Session, Depends(get_db)], user: Annotated[User, Depends(get_current_user)]):
    query = select(Notification).where(Notification.read.is_(False))
    if user.role != "admin":
        query = query.join(DeviceAccess, DeviceAccess.device_uid == Notification.device_uid).where(DeviceAccess.user_id == user.id)
    for notification in db.scalars(query):
        notification.read = True
    db.commit()


@app.post("/api/v1/notifications/{notification_id}/read", status_code=204)
def read_notification(notification_id: int, db: Annotated[Session, Depends(get_db)], user: Annotated[User, Depends(get_current_user)]):
    notification = db.get(Notification, notification_id)
    if not notification:
        raise HTTPException(404, "Notification not found")
    if not can_access_device(db, user, notification.device_uid):
        raise HTTPException(403, "Device access denied")
    notification.read = True
    db.commit()
