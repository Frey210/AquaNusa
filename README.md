# AquaNusa

Dashboard PWA untuk memantau kualitas air dan lingkungan: suhu air, suhu udara,
dissolved oxygen (DO), pH, kelembapan, dan illuminance.

## Menjalankan lokal

### Backend

```powershell
cd backend
python -m venv .venv
.venv\Scripts\pip install -r requirements.txt
Copy-Item .env.example .env
.venv\Scripts\uvicorn app.main:app --reload
```

API docs tersedia di `http://127.0.0.1:8000/docs`.

### Frontend

```powershell
cd frontend
npm install
npm run dev
```

Buka `http://127.0.0.1:5173`. Vite meneruskan `/api` ke backend.

### Kirim data contoh

```powershell
Invoke-RestMethod http://127.0.0.1:8000/api/v1/telemetry -Method Post `
  -Headers @{ 'X-Device-Key' = 'change-me' } -ContentType application/json `
  -Body '{"uid":"AQUANUSA-001","water_temp_c":28.4,"air_temp_c":31.2,"do_mg_l":6.8,"ph":7.4,"humidity_rh":78.0,"illuminance_lux":18420}'
```

## Firmware

Edit `WIFI_PORTAL_NAME`, `API_URL`, `DEVICE_UID`, dan pin pada
`frimware/src/main.cpp`, lalu:

```powershell
cd frimware
pio run
```

Sensor RS485 mengikuti datasheet di `frimware/datasheet`: pH slave `0x03`, DO
slave `0x0A`, dan sensor lingkungan slave `0x02`. Offset/scale kalibrasi sengaja
tersedia di bagian atas firmware.

## Konfigurasi produksi

- Ganti `AQUANUSA_DEVICE_KEY` pada backend dan firmware.
- Batasi `CORS_ORIGINS` ke domain frontend.
- Jalankan backend di balik HTTPS reverse proxy.

