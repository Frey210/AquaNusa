import importlib

from fastapi.testclient import TestClient


def test_telemetry_round_trip(tmp_path, monkeypatch):
    monkeypatch.setenv("DATABASE_URL", f"sqlite:///{tmp_path / 'test.db'}")
    monkeypatch.setenv("AQUANUSA_DEVICE_KEY", "test-key")
    main = importlib.import_module("app.main")
    with TestClient(main.app) as client:
        payload = {"uid": "AQUANUSA-001", "water_temp_c": 28.4, "air_temp_c": 31.2, "do_mg_l": 6.8, "ph": 7.4, "humidity_rh": 78, "illuminance_lux": 18420}
        assert client.post("/api/v1/telemetry", json=payload).status_code == 401
        assert client.post("/api/v1/telemetry", json=payload, headers={"X-Device-Key": "test-key"}).status_code == 201
        device = client.get("/api/v1/devices").json()[0]
        assert device["online"] is True
        assert device["latest"]["ph"] == 7.4
        assert len(client.get("/api/v1/devices/AQUANUSA-001/history").json()) == 1

