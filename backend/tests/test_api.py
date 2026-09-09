import importlib

from fastapi.testclient import TestClient


def test_auth_roles_and_device_access(tmp_path, monkeypatch):
    monkeypatch.setenv("DATABASE_URL", f"sqlite:///{tmp_path / 'test.db'}")
    monkeypatch.setenv("AQUANUSA_DEVICE_KEY", "test-key")
    monkeypatch.setenv("COOKIE_SECURE", "false")
    monkeypatch.setenv("INITIAL_DEVICE_UID", "AQUANUSA-001")
    monkeypatch.setenv("AQUANUSA_ADMIN_EMAIL", "admin@example.test")
    monkeypatch.setenv("AQUANUSA_ADMIN_PASSWORD", "Admin-password-123!")
    monkeypatch.setenv("AQUANUSA_ADMIN_NAME", "Admin")
    monkeypatch.setenv("AQUANUSA_USER_EMAIL", "user@example.test")
    monkeypatch.setenv("AQUANUSA_USER_PASSWORD", "User-password-123!")
    monkeypatch.setenv("AQUANUSA_USER_NAME", "Operator")

    import app.main
    main = importlib.reload(app.main)
    payload = {"water_temp_c": 28.4, "air_temp_c": 31.2, "do_mg_l": 6.8, "ph": 7.4,
               "humidity_rh": 78, "illuminance_lux": 18420}

    with TestClient(main.app) as anonymous:
        assert anonymous.get("/api/v1/devices").status_code == 401
        for uid in ("AQUANUSA-001", "AQUANUSA-002"):
            assert anonymous.post("/api/v1/telemetry", json={"uid": uid, **payload},
                                  headers={"X-Device-Key": "test-key"}).status_code == 201

    with TestClient(main.app) as user:
        assert user.post("/api/v1/auth/login", json={"email": "user@example.test", "password": "User-password-123!"}).status_code == 200
        assert [device["uid"] for device in user.get("/api/v1/devices").json()] == ["AQUANUSA-001"]
        assert user.get("/api/v1/devices/AQUANUSA-002/history").status_code == 403
        assert user.post("/api/v1/auth/logout").status_code == 204
        assert user.get("/api/v1/auth/me").status_code == 401

    with TestClient(main.app) as admin:
        session = admin.post("/api/v1/auth/login", json={"email": "admin@example.test", "password": "Admin-password-123!"})
        assert session.status_code == 200 and session.json()["role"] == "admin"
        assert len(admin.get("/api/v1/devices").json()) == 2
