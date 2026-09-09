import { useEffect, useMemo, useState } from "react";
import { createRoot } from "react-dom/client";
import {
  CloudSun, Drop, Gauge, Leaf, Lightbulb, MapPin, Pulse, Thermometer,
  Waves, WifiHigh, WifiSlash,
} from "@phosphor-icons/react";
import type { Icon } from "@phosphor-icons/react";
import "./styles.css";

type Reading = {
  id: number; uid: string; recorded_at: string; water_temp_c: number; air_temp_c: number;
  do_mg_l: number; ph: number; humidity_rh: number; illuminance_lux: number;
};
type Device = { uid: string; name: string | null; last_seen: string | null; online: boolean; latest: Reading | null };
type Key = "water_temp_c" | "air_temp_c" | "do_mg_l" | "ph" | "humidity_rh" | "illuminance_lux";

const metrics: { key: Key; label: string; unit: string; icon: Icon; tone: string }[] = [
  { key: "water_temp_c", label: "Suhu air", unit: "°C", icon: Waves, tone: "cyan" },
  { key: "air_temp_c", label: "Suhu udara", unit: "°C", icon: Thermometer, tone: "orange" },
  { key: "do_mg_l", label: "Dissolved oxygen", unit: "mg/L", icon: Drop, tone: "blue" },
  { key: "ph", label: "pH air", unit: "pH", icon: Gauge, tone: "violet" },
  { key: "humidity_rh", label: "Kelembapan", unit: "%RH", icon: CloudSun, tone: "green" },
  { key: "illuminance_lux", label: "Illuminance", unit: "Lux", icon: Lightbulb, tone: "yellow" },
];

function Sparkline({ data, field }: { data: Reading[]; field: Key }) {
  if (data.length < 2) return <div className="chart-empty">Belum cukup data tren</div>;
  const values = data.map((row) => row[field]);
  const min = Math.min(...values), max = Math.max(...values), span = max - min || 1;
  const points = values.map((value, index) => `${(index / (values.length - 1)) * 100},${44 - ((value - min) / span) * 36}`).join(" ");
  return <svg className="spark" viewBox="0 0 100 48" preserveAspectRatio="none" role="img" aria-label={`Tren ${field}`}><polyline points={points} /></svg>;
}

function App() {
  const [devices, setDevices] = useState<Device[]>([]);
  const [selected, setSelected] = useState("");
  const [history, setHistory] = useState<Reading[]>([]);
  const [error, setError] = useState("");

  useEffect(() => {
    let active = true;
    const load = () => fetch("/api/v1/devices").then((response) => {
      if (!response.ok) throw new Error("API tidak tersedia");
      return response.json() as Promise<Device[]>;
    }).then((data) => {
      if (!active) return;
      setDevices(data); setSelected((current) => current || data[0]?.uid || ""); setError("");
    }).catch(() => active && setError("Koneksi ke server terputus. Menampilkan data terakhir jika tersedia."));
    load(); const timer = window.setInterval(load, 15_000);
    return () => { active = false; window.clearInterval(timer); };
  }, []);

  useEffect(() => {
    if (!selected) return;
    fetch(`/api/v1/devices/${encodeURIComponent(selected)}/history?hours=24&limit=288`)
      .then((response) => response.ok ? response.json() : Promise.reject())
      .then(setHistory).catch(() => undefined);
  }, [selected, devices]);

  const device = devices.find((item) => item.uid === selected);
  const latest = device?.latest;
  const lastUpdate = latest ? new Intl.DateTimeFormat("id-ID", { hour: "2-digit", minute: "2-digit", second: "2-digit" }).format(new Date(latest.recorded_at)) : "—";
  const healthy = useMemo(() => latest ? latest.ph >= 6.5 && latest.ph <= 8.5 && latest.do_mg_l >= 5 : false, [latest]);

  return <div className="app-shell">
    <aside>
      <a className="brand" href="/" aria-label="AquaNusa beranda"><img src="/logo.png" alt="" /><span>Aqua<span>Nusa</span></span></a>
      <nav aria-label="Navigasi utama"><a className="active" href="#monitor"><Pulse size={22} aria-hidden="true" /> Monitoring</a></nav>
      <div className="aside-note"><Leaf size={24} aria-hidden="true" /><strong>Teknologi untuk perairan</strong><span>Data yang lebih jernih untuk keputusan yang lebih baik.</span></div>
    </aside>

    <main id="monitor">
      <header>
        <div><p className="eyebrow">AquaNusa Water Intelligence</p><h1>Kondisi perairan, dalam satu pandangan.</h1></div>
        <label className="device-select"><span>Perangkat</span><select value={selected} onChange={(event) => setSelected(event.target.value)}>{devices.map((item) => <option key={item.uid} value={item.uid}>{item.name || item.uid}</option>)}</select></label>
      </header>

      {error && <div className="notice" role="status"><WifiSlash size={20} aria-hidden="true" />{error}</div>}

      {!latest ? <section className="empty-state"><div className="empty-icon"><Waves size={38} aria-hidden="true" /></div><h2>Menunggu data pertama</h2><p>Nyalakan perangkat AquaNusa atau kirim payload contoh dari README. Dashboard akan memperbarui otomatis.</p></section> : <>
        <section className="status-strip" aria-label="Ringkasan status">
          <div className={`live ${device?.online ? "online" : "offline"}`}>{device?.online ? <WifiHigh size={20} aria-hidden="true" /> : <WifiSlash size={20} aria-hidden="true" />}<span><strong>{device?.online ? "Perangkat online" : "Perangkat offline"}</strong>Terakhir diperbarui {lastUpdate}</span></div>
          <div><MapPin size={20} aria-hidden="true" /><span><strong>{device?.name || device?.uid}</strong>Unit pemantauan aktif</span></div>
          <div className={healthy ? "quality-good" : "quality-watch"}><Leaf size={20} aria-hidden="true" /><span><strong>{healthy ? "Kondisi stabil" : "Perlu perhatian"}</strong>Berdasarkan pH dan DO</span></div>
        </section>

        <section className="metric-grid" aria-label="Pembacaan sensor terkini">
          {metrics.map(({ key, label, unit, icon: MetricIcon, tone }) => <article className={`metric-card ${tone}`} key={key}>
            <div className="metric-head"><span>{label}</span><MetricIcon size={22} aria-hidden="true" /></div>
            <div className="metric-value">{latest[key].toLocaleString("id-ID", { maximumFractionDigits: key === "illuminance_lux" ? 0 : 1 })}<small>{unit}</small></div>
            <Sparkline data={history} field={key} />
          </article>)}
        </section>

        <section className="insight">
          <div><p className="eyebrow">Ringkasan 24 jam</p><h2>Air memberi sinyal. AquaNusa membuatnya terbaca.</h2><p>Grafik kecil pada setiap kartu merangkum hingga 288 pembacaan terakhir tanpa membebani koneksi seluler.</p></div>
          <div className="insight-stat"><span>{history.length}</span><small>sampel tersimpan</small></div>
        </section>
      </>}
      <footer>© {new Date().getFullYear()} AquaNusa · Aerasea</footer>
    </main>
  </div>;
}

if ("serviceWorker" in navigator && import.meta.env.PROD) navigator.serviceWorker.register("/sw.js");
createRoot(document.getElementById("root")!).render(<App />);

