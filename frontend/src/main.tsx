import { FormEvent, useCallback, useEffect, useMemo, useState } from "react";
import { createRoot } from "react-dom/client";
import {
  CloudSun, Drop, Eye, EyeSlash, Gauge, Leaf, Lightbulb, LockKey, MapPin,
  Pulse, SignOut, Thermometer, UserCircle, Waves, WifiHigh, WifiSlash,
} from "@phosphor-icons/react";
import type { Icon } from "@phosphor-icons/react";
import "./styles.css";

type User = { id: number; name: string; email: string; role: "admin" | "user" };
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

async function api<T>(path: string, init?: RequestInit): Promise<T> {
  const response = await fetch(path, { credentials: "same-origin", ...init, headers: { "Content-Type": "application/json", ...init?.headers } });
  if (!response.ok) {
    const body = await response.json().catch(() => null) as { detail?: string } | null;
    throw new Error(body?.detail || "Permintaan gagal");
  }
  return response.status === 204 ? undefined as T : response.json();
}

function Sparkline({ data, field }: { data: Reading[]; field: Key }) {
  if (data.length < 2) return <div className="chart-empty">Belum cukup data tren</div>;
  const values = data.map((row) => row[field]);
  const min = Math.min(...values), max = Math.max(...values), span = max - min || 1;
  const points = values.map((value, index) => `${(index / (values.length - 1)) * 100},${44 - ((value - min) / span) * 36}`).join(" ");
  return <svg className="spark" viewBox="0 0 100 48" preserveAspectRatio="none" role="img" aria-label={`Tren ${field}`}><polyline points={points} /></svg>;
}

function Login({ onLogin }: { onLogin: (user: User) => void }) {
  const [showPassword, setShowPassword] = useState(false);
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState("");

  async function submit(event: FormEvent<HTMLFormElement>) {
    event.preventDefault();
    setSubmitting(true); setError("");
    const data = new FormData(event.currentTarget);
    try {
      onLogin(await api<User>("/api/v1/auth/login", { method: "POST", body: JSON.stringify({ email: data.get("email"), password: data.get("password") }) }));
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "Tidak dapat masuk");
    } finally { setSubmitting(false); }
  }

  return <main className="login-page">
    <section className="login-brand" aria-label="Tentang AquaNusa">
      <img src="/logo.png" alt="" />
      <p className="eyebrow">AquaNusa Water Intelligence</p>
      <h1>Data perairan yang tepat, untuk keputusan yang lebih tenang.</h1>
      <p>Masuk untuk memantau suhu, oksigen terlarut, pH, kelembapan, dan cahaya dari perangkat Anda.</p>
    </section>
    <section className="login-panel">
      <form className="login-form" onSubmit={submit}>
        <div className="auth-heading"><LockKey size={28} aria-hidden="true" /><div><p className="eyebrow">AKSES TERLINDUNGI</p><h2>Masuk ke AquaNusa</h2></div></div>
        {error && <div className="form-error" role="alert">{error}</div>}
        <label htmlFor="email">Email</label>
        <input id="email" name="email" type="email" autoComplete="username" required autoFocus />
        <label htmlFor="password">Password</label>
        <div className="password-field"><input id="password" name="password" type={showPassword ? "text" : "password"} autoComplete="current-password" required minLength={8} />
          <button type="button" onClick={() => setShowPassword((value) => !value)} aria-label={showPassword ? "Sembunyikan password" : "Tampilkan password"} aria-pressed={showPassword}>{showPassword ? <EyeSlash size={20} /> : <Eye size={20} />}</button>
        </div>
        <button className="login-submit" disabled={submitting}>{submitting ? "Memverifikasi…" : "Masuk"}</button>
        <p className="auth-note">Gunakan akun yang diberikan administrator AquaNusa.</p>
      </form>
    </section>
  </main>;
}

function Dashboard({ user, onLogout }: { user: User; onLogout: () => void }) {
  const [devices, setDevices] = useState<Device[]>([]);
  const [selected, setSelected] = useState("");
  const [history, setHistory] = useState<Reading[]>([]);
  const [error, setError] = useState("");

  const loadDevices = useCallback(() => api<Device[]>("/api/v1/devices").then((data) => {
    setDevices(data); setSelected((current) => data.some((item) => item.uid === current) ? current : data[0]?.uid || ""); setError("");
  }).catch((reason: Error) => setError(reason.message)), []);

  useEffect(() => { loadDevices(); const timer = window.setInterval(loadDevices, 15_000); return () => window.clearInterval(timer); }, [loadDevices]);
  useEffect(() => {
    if (!selected) return setHistory([]);
    api<Reading[]>(`/api/v1/devices/${encodeURIComponent(selected)}/history?hours=24&limit=288`).then(setHistory).catch((reason: Error) => setError(reason.message));
  }, [selected, devices]);

  const device = devices.find((item) => item.uid === selected);
  const latest = device?.latest;
  const lastUpdate = latest ? new Intl.DateTimeFormat("id-ID", { hour: "2-digit", minute: "2-digit", second: "2-digit" }).format(new Date(latest.recorded_at)) : "—";
  const healthy = useMemo(() => latest ? latest.ph >= 6.5 && latest.ph <= 8.5 && latest.do_mg_l >= 5 : false, [latest]);

  return <div className="app-shell">
    <aside>
      <a className="brand" href="#monitor" aria-label="AquaNusa beranda"><img src="/logo.png" alt="" /><span>Aqua<span>Nusa</span></span></a>
      <nav aria-label="Navigasi utama"><a className="active" href="#monitor"><Pulse size={22} aria-hidden="true" /> Monitoring</a></nav>
      <div className="account-card"><UserCircle size={25} aria-hidden="true" /><span><strong>{user.name}</strong><small>{user.role === "admin" ? "Administrator" : "Pengguna device"}</small></span><button onClick={onLogout} aria-label="Keluar dari akun"><SignOut size={21} /></button></div>
      <div className="aside-note"><Leaf size={24} aria-hidden="true" /><strong>Teknologi untuk perairan</strong><span>Data yang lebih jernih untuk keputusan yang lebih baik.</span></div>
    </aside>

    <main id="monitor">
      <header><div><p className="eyebrow">{user.role === "admin" ? "Portal Administrator" : "Portal Pengguna"}</p><h1>Kondisi perairan, dalam satu pandangan.</h1></div>
        <label className="device-select"><span>Perangkat</span><select value={selected} onChange={(event) => setSelected(event.target.value)} disabled={devices.length < 2}>{devices.map((item) => <option key={item.uid} value={item.uid}>{item.name || item.uid}</option>)}</select></label>
      </header>
      {error && <div className="notice" role="status"><WifiSlash size={20} aria-hidden="true" />{error}</div>}
      {!latest ? <section className="empty-state"><div className="empty-icon"><Waves size={38} aria-hidden="true" /></div><h2>{devices.length ? "Menunggu data terbaru" : "Belum ada perangkat"}</h2><p>{user.role === "admin" ? "Device akan muncul setelah mengirim telemetri pertama." : "Hubungi administrator bila perangkat belum terhubung ke akun Anda."}</p></section> : <>
        <section className="status-strip" aria-label="Ringkasan status">
          <div className={`live ${device?.online ? "online" : "offline"}`}>{device?.online ? <WifiHigh size={20} aria-hidden="true" /> : <WifiSlash size={20} aria-hidden="true" />}<span><strong>{device?.online ? "Perangkat online" : "Perangkat offline"}</strong>Terakhir diperbarui {lastUpdate}</span></div>
          <div><MapPin size={20} aria-hidden="true" /><span><strong>{device?.name || device?.uid}</strong>Unit pemantauan aktif</span></div>
          <div className={healthy ? "quality-good" : "quality-watch"}><Leaf size={20} aria-hidden="true" /><span><strong>{healthy ? "Kondisi stabil" : "Perlu perhatian"}</strong>Berdasarkan pH dan DO</span></div>
        </section>
        <section className="metric-grid" aria-label="Pembacaan sensor terkini">{metrics.map(({ key, label, unit, icon: MetricIcon, tone }) => <article className={`metric-card ${tone}`} key={key}><div className="metric-head"><span>{label}</span><MetricIcon size={22} aria-hidden="true" /></div><div className="metric-value">{latest[key].toLocaleString("id-ID", { maximumFractionDigits: key === "illuminance_lux" ? 0 : 1 })}<small>{unit}</small></div><Sparkline data={history} field={key} /></article>)}</section>
        <section className="insight"><div><p className="eyebrow">Ringkasan 24 jam</p><h2>Air memberi sinyal. AquaNusa membuatnya terbaca.</h2><p>Grafik kecil pada setiap kartu merangkum hingga 288 pembacaan terakhir tanpa membebani koneksi seluler.</p></div><div className="insight-stat"><span>{history.length}</span><small>sampel tersimpan</small></div></section>
      </>}
      <footer>© {new Date().getFullYear()} AquaNusa · Aerasea</footer>
    </main>
  </div>;
}

function App() {
  const [user, setUser] = useState<User | null>(null);
  const [checking, setChecking] = useState(true);
  useEffect(() => { api<User>("/api/v1/auth/me").then(setUser).catch(() => undefined).finally(() => setChecking(false)); }, []);
  if (checking) return <div className="app-loading" role="status"><img src="/logo.png" alt="" /><span>Memuat AquaNusa…</span></div>;
  if (!user) return <Login onLogin={setUser} />;
  return <Dashboard user={user} onLogout={() => api<void>("/api/v1/auth/logout", { method: "POST" }).finally(() => setUser(null))} />;
}

if ("serviceWorker" in navigator && import.meta.env.PROD) navigator.serviceWorker.register("/sw.js");
createRoot(document.getElementById("root")!).render(<App />);
