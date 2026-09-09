import hmac
import os
from contextlib import asynccontextmanager
from datetime import datetime, timedelta, timezone
from typing import Annotated

from fastapi import Depends, FastAPI, Header, HTTPException, Query
from fastapi.middleware.cors import CORSMiddleware
from dotenv import load_dotenv
from pydantic import BaseModel, ConfigDict, Field
from sqlalchemy import DateTime, Float, ForeignKey, String, create_engine, desc, select
from sqlalchemy.orm import DeclarativeBase, Mapped, Session, mapped_column, relationship, sessionmaker


load_dotenv()
DATABASE_URL = os.getenv("DATABASE_URL", "sqlite:///./aquanusa.db")
DEVICE_KEY = os.getenv("AQUANUSA_DEVICE_KEY", "change-me")
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


def get_db():
    with SessionLocal() as db:
        yield db


def require_device_key(x_device_key: Annotated[str | None, Header()] = None):
    if not x_device_key or not hmac.compare_digest(x_device_key, DEVICE_KEY):
        raise HTTPException(401, "Invalid device key")


def reading_out(uid: str, reading: Reading) -> ReadingOut:
    recorded_at = reading.recorded_at
    if recorded_at.tzinfo is None:
        recorded_at = recorded_at.replace(tzinfo=timezone.utc)
    return ReadingOut(uid=uid, **{**reading.__dict__, "recorded_at": recorded_at})


@asynccontextmanager
async def lifespan(_: FastAPI):
    Base.metadata.create_all(engine)
    yield


app = FastAPI(title="AquaNusa API", version="0.1.0", lifespan=lifespan)
origins = [value.strip() for value in os.getenv("CORS_ORIGINS", "http://localhost:5173").split(",") if value.strip()]
app.add_middleware(CORSMiddleware, allow_origins=origins, allow_methods=["GET", "POST"], allow_headers=["Content-Type", "X-Device-Key"])


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
    device.last_seen = now
    reading = Reading(**payload.model_dump(exclude={"uid"}), device_uid=payload.uid, recorded_at=now)
    db.add(reading)
    db.commit()
    db.refresh(reading)
    return reading_out(payload.uid, reading)


@app.get("/api/v1/devices", response_model=list[DeviceOut])
def list_devices(db: Annotated[Session, Depends(get_db)]):
    devices = db.scalars(select(Device).order_by(Device.name)).all()
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
def device_history(uid: str, db: Annotated[Session, Depends(get_db)], hours: Annotated[int, Query(ge=1, le=168)] = 24, limit: Annotated[int, Query(ge=1, le=5000)] = 500):
    if not db.get(Device, uid):
        raise HTTPException(404, "Device not found")
    readings = db.scalars(select(Reading).where(Reading.device_uid == uid, Reading.recorded_at >= datetime.now(timezone.utc) - timedelta(hours=hours)).order_by(Reading.recorded_at).limit(limit)).all()
    return [reading_out(uid, reading) for reading in readings]
