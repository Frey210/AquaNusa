import { FormEvent, useCallback, useEffect, useMemo, useState } from "react";
import { createRoot } from "react-dom/client";
import {
  Bell, CheckCircle, ClockCounterClockwise, CloudSun, DownloadSimple, Drop, Eye,
  EyeSlash, Gauge, Leaf, Lightbulb, LockKey, MapPin, Pulse, SignOut, Sliders,
  Thermometer, UserCircle, Waves, WifiHigh, WifiSlash,
} from "@phosphor-icons/react";
import type { Icon } from "@phosphor-icons/react";
import { requestPushToken } from "./firebase";
import "./styles.css";

type User = { id: number; name: string; email: string; role: "admin" | "user" };
type Reading = {
  id: number; uid: string; recorded_at: string; water_temp_c: number; air_temp_c: number;
  do_mg_l: number; ph: number; humidity_rh: number; illuminance_lux: number;
};
type Device = { uid: string; name: string | null; last_seen: string | null; online: boolean; latest: Reading | null };
type Key = "water_temp_c" | "air_temp_c" | "do_mg_l" | "ph" | "humidity_rh" | "illuminance_lux";
type View = "monitor" | "history" | "thresholds" | "notifications";
type Threshold = Record<`${Key}_${"min" | "max"}`, number> & { device_uid: string };
type Alert = { id: number; device_uid: string; parameter: Key; direction: "low" | "high"; value: number; threshold: number; message: string; read: boolean; created_at: string };

const metrics: { key: Key; label: string; unit: string; icon: Icon; tone: string; step: number }[] = [
  { key: "water_temp_c", label: "Suhu air", unit: "°C", icon: Waves, tone: "cyan", step: .1 },
  { key: "air_temp_c", label: "Suhu udara", unit: "°C", icon: Thermometer, tone: "orange", step: .1 },
  { key: "do_mg_l", label: "Dissolved oxygen", unit: "mg/L", icon: Drop, tone: "blue", step: .1 },
  { key: "ph", label: "pH air", unit: "pH", icon: Gauge, tone: "violet", step: .1 },
  { key: "humidity_rh", label: "Kelembapan", unit: "%RH", icon: CloudSun, tone: "green", step: .1 },
  { key: "illuminance_lux", label: "Illuminance", unit: "Lux", icon: Lightbulb, tone: "yellow", step: 1 },
];

async function api<T>(path: string, init?: RequestInit): Promise<T> {
  const response = await fetch(path, { credentials: "same-origin", ...init, headers: { "Content-Type": "application/json", ...init?.headers } });
  if (!response.ok) {
    const body = await response.json().catch(() => null) as { detail?: string } | null;
    throw new Error(body?.detail || "Permintaan gagal");
  }
  return response.status === 204 ? undefined as T : response.json();
}

const formatDate = (value: string) => new Intl.DateTimeFormat("id-ID", { dateStyle: "medium", timeStyle: "medium" }).format(new Date(value));
const formatValue = (value: number, key: Key) => value.toLocaleString("id-ID", { maximumFractionDigits: key === "illuminance_lux" ? 0 : 1 });

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
    event.preventDefault(); setSubmitting(true); setError("");
    const data = new FormData(event.currentTarget);
    try { onLogin(await api<User>("/api/v1/auth/login", { method: "POST", body: JSON.stringify({ email: data.get("email"), password: data.get("password") }) })); }
    catch (reason) { setError(reason instanceof Error ? reason.message : "Tidak dapat masuk"); }
    finally { setSubmitting(false); }
  }
  return <main className="login-page">
    <section className="login-brand" aria-label="Tentang AquaNusa"><img src="/logo.png" alt="" /><p className="eyebrow">AquaNusa Water Intelligence</p><h1>Data perairan yang tepat, untuk keputusan yang lebih tenang.</h1><p>Masuk untuk memantau suhu, oksigen terlarut, pH, kelembapan, dan cahaya dari perangkat Anda.</p></section>
    <section className="login-panel"><form className="login-form" onSubmit={submit}><div className="auth-heading"><LockKey size={28} aria-hidden="true" /><div><p className="eyebrow">Akses terlindungi</p><h2>Masuk ke AquaNusa</h2></div></div>{error && <div className="form-error" role="alert">{error}</div>}<label htmlFor="email">Email</label><input id="email" name="email" type="email" autoComplete="username" required autoFocus /><label htmlFor="password">Password</label><div className="password-field"><input id="password" name="password" type={showPassword ? "text" : "password"} autoComplete="current-password" required minLength={8} /><button type="button" onClick={() => setShowPassword((value) => !value)} aria-label={showPassword ? "Sembunyikan password" : "Tampilkan password"} aria-pressed={showPassword}>{showPassword ? <EyeSlash size={20} /> : <Eye size={20} />}</button></div><button className="login-submit" disabled={submitting}>{submitting ? "Memverifikasi…" : "Masuk"}</button><p className="auth-note">Gunakan akun yang diberikan administrator AquaNusa.</p></form></section>
  </main>;
}

function Dashboard({ user, onLogout }: { user: User; onLogout: () => void }) {
  const [view, setView] = useState<View>("monitor"), [devices, setDevices] = useState<Device[]>([]);
  const [selected, setSelected] = useState(""), [history, setHistory] = useState<Reading[]>([]);
  const [hours, setHours] = useState(24), [threshold, setThreshold] = useState<Threshold | null>(null);
  const [alerts, setAlerts] = useState<Alert[]>([]), [message, setMessage] = useState(""), [error, setError] = useState("");
  const [pushEnabled, setPushEnabled] = useState(typeof Notification !== "undefined" && Notification.permission === "granted");

  useEffect(() => window.scrollTo(0, 0), []);

  const loadDevices = useCallback(() => api<Device[]>("/api/v1/devices").then((data) => {
    setDevices(data); setSelected((current) => data.some((item) => item.uid === current) ? current : data[0]?.uid || ""); setError("");
  }).catch((reason: Error) => setError(reason.message)), []);
  const loadAlerts = useCallback(() => api<Alert[]>("/api/v1/notifications?limit=500").then(setAlerts).catch((reason: Error) => setError(reason.message)), []);

  useEffect(() => { loadDevices(); loadAlerts(); const timer = window.setInterval(() => { loadDevices(); loadAlerts(); }, 15_000); return () => window.clearInterval(timer); }, [loadDevices, loadAlerts]);
  useEffect(() => {
    if (!selected) { setHistory([]); setThreshold(null); return; }
    const load = () => api<Reading[]>(`/api/v1/devices/${encodeURIComponent(selected)}/history?hours=${hours}&limit=5000`).then(setHistory).catch((reason: Error) => setError(reason.message));
    load(); const timer = window.setInterval(load, 15_000); return () => window.clearInterval(timer);
  }, [selected, hours]);
  useEffect(() => { if (selected) api<Threshold>(`/api/v1/devices/${encodeURIComponent(selected)}/thresholds`).then(setThreshold).catch((reason: Error) => setError(reason.message)); }, [selected]);

  const device = devices.find((item) => item.uid === selected), latest = device?.latest;
  const unread = alerts.filter((alert) => !alert.read).length;
  const lastUpdate = latest ? new Intl.DateTimeFormat("id-ID", { hour: "2-digit", minute: "2-digit", second: "2-digit" }).format(new Date(latest.recorded_at)) : "—";
  const healthy = useMemo(() => latest ? latest.ph >= (threshold?.ph_min ?? 6.5) && latest.ph <= (threshold?.ph_max ?? 8.5) && latest.do_mg_l >= (threshold?.do_mg_l_min ?? 5) : false, [latest, threshold]);

  async function saveThreshold(event: FormEvent) {
    event.preventDefault(); if (!threshold || !selected) return;
    const { device_uid: _, ...payload } = threshold;
    try { setThreshold(await api<Threshold>(`/api/v1/devices/${encodeURIComponent(selected)}/thresholds`, { method: "PUT", body: JSON.stringify(payload) })); setMessage("Ambang batas tersimpan."); setError(""); }
    catch (reason) { setError(reason instanceof Error ? reason.message : "Gagal menyimpan ambang batas"); }
  }
  async function exportCsv() {
    if (!selected) return;
    const response = await fetch(`/api/v1/devices/${encodeURIComponent(selected)}/export.csv?hours=${hours}`, { credentials: "same-origin" });
    if (!response.ok) return setError("Gagal mengekspor data");
    const url = URL.createObjectURL(await response.blob()), link = document.createElement("a");
    link.href = url; link.download = `aquanusa-${selected}.csv`; link.click(); URL.revokeObjectURL(url);
  }
  async function markRead(id?: number) {
    await api<void>(id ? `/api/v1/notifications/${id}/read` : "/api/v1/notifications/read-all", { method: "POST" });
    setAlerts((items) => items.map((item) => !id || item.id === id ? { ...item, read: true } : item));
  }
  async function enablePush() {
    try {
      const token = await requestPushToken();
      if (!token) throw new Error("Token notifikasi tidak tersedia");
      await api<void>("/api/v1/notifications/push-token", { method: "PUT", body: JSON.stringify({ token }) });
      setPushEnabled(true); setMessage("Push notification aktif pada perangkat ini."); setError("");
    } catch (reason) { setError(reason instanceof Error ? reason.message : "Gagal mengaktifkan push notification"); }
  }

  const titles: Record<View, string> = { monitor: "Kondisi perairan, dalam satu pandangan.", history: "Riwayat data sensor.", thresholds: "Ambang batas perangkat.", notifications: "Log notifikasi sensor." };
  const nav: { id: View; label: string; icon: Icon }[] = [
    { id: "monitor", label: "Monitoring", icon: Pulse }, { id: "history", label: "Riwayat", icon: ClockCounterClockwise },
    { id: "thresholds", label: "Ambang batas", icon: Sliders }, { id: "notifications", label: "Notifikasi", icon: Bell },
  ];

  return <div className="app-shell">
    <aside><button className="brand" onClick={() => setView("monitor")} aria-label="AquaNusa beranda"><img src="/logo.png" alt="" /><span>Aqua<span>Nusa</span></span></button><nav aria-label="Navigasi utama">{nav.map(({ id, label, icon: NavIcon }) => <button key={id} className={view === id ? "active" : ""} onClick={() => { setView(id); setMessage(""); }} aria-label={label} aria-current={view === id ? "page" : undefined}><NavIcon size={22} aria-hidden="true" /><span>{label}</span>{id === "notifications" && unread > 0 && <b>{unread}</b>}</button>)}</nav><div className="account-card"><UserCircle size={25} aria-hidden="true" /><span><strong>{user.name}</strong><small>{user.role === "admin" ? "Administrator" : "Pengguna device"}</small></span><button onClick={onLogout} aria-label="Keluar dari akun"><SignOut size={21} /></button></div><div className="aside-note"><Leaf size={24} aria-hidden="true" /><strong>Teknologi untuk perairan</strong><span>Data yang lebih jernih untuk keputusan yang lebih baik.</span></div></aside>
    <main><header><div><p className="eyebrow">{user.role === "admin" ? "Portal Administrator" : "Portal Pengguna"}</p><h1>{titles[view]}</h1></div>{view !== "notifications" && <label className="device-select"><span>Perangkat</span><select value={selected} onChange={(event) => setSelected(event.target.value)} disabled={devices.length < 2}>{devices.map((item) => <option key={item.uid} value={item.uid}>{item.name || item.uid}</option>)}</select></label>}</header>
      {error && <div className="notice" role="alert"><WifiSlash size={20} aria-hidden="true" />{error}</div>}{message && <div className="success" role="status"><CheckCircle size={20} aria-hidden="true" />{message}</div>}
      {view === "monitor" && (!latest ? <Empty hasDevice={devices.length > 0} admin={user.role === "admin"} /> : <><section className="status-strip" aria-label="Ringkasan status"><div className={`live ${device?.online ? "online" : "offline"}`}>{device?.online ? <WifiHigh size={20} aria-hidden="true" /> : <WifiSlash size={20} aria-hidden="true" />}<span><strong>{device?.online ? "Perangkat online" : "Perangkat offline"}</strong>Terakhir diperbarui {lastUpdate}</span></div><div><MapPin size={20} aria-hidden="true" /><span><strong>{device?.name || device?.uid}</strong>Unit pemantauan aktif</span></div><div className={healthy ? "quality-good" : "quality-watch"}><Leaf size={20} aria-hidden="true" /><span><strong>{healthy ? "Kondisi stabil" : "Perlu perhatian"}</strong>Berdasarkan ambang pH dan DO</span></div></section><section className="metric-grid" aria-label="Pembacaan sensor terkini">{metrics.map(({ key, label, unit, icon: MetricIcon, tone }) => <article className={`metric-card ${tone}`} key={key}><div className="metric-head"><span>{label}</span><MetricIcon size={22} aria-hidden="true" /></div><div className="metric-value">{formatValue(latest[key], key)}<small>{unit}</small></div><Sparkline data={history} field={key} /></article>)}</section><section className="insight"><div><p className="eyebrow">Ringkasan 24 jam</p><h2>Air memberi sinyal. AquaNusa membuatnya terbaca.</h2><p>Grafik kecil pada setiap kartu merangkum pembacaan terakhir tanpa membebani koneksi seluler.</p></div><div className="insight-stat"><span>{history.length}</span><small>sampel tersimpan</small></div></section></>)}
      {view === "history" && <section className="panel"><div className="panel-tools"><label>Rentang data<select value={hours} onChange={(event) => setHours(Number(event.target.value))}><option value={24}>24 jam</option><option value={72}>3 hari</option><option value={168}>7 hari</option></select></label><button className="primary" onClick={exportCsv} disabled={!history.length}><DownloadSimple size={20} /> Ekspor CSV</button></div><p className="panel-caption">{history.length} pembacaan · data terbaru ditampilkan lebih dahulu</p><div className="table-wrap"><table><thead><tr><th>Waktu</th>{metrics.map((metric) => <th key={metric.key}>{metric.label}<small>{metric.unit}</small></th>)}</tr></thead><tbody>{[...history].reverse().map((row) => <tr key={row.id}><td>{formatDate(row.recorded_at)}</td>{metrics.map((metric) => <td key={metric.key}>{formatValue(row[metric.key], metric.key)}</td>)}</tr>)}</tbody></table>{!history.length && <p className="empty-row">Belum ada data pada rentang ini.</p>}</div></section>}
      {view === "thresholds" && <section className="panel"><p className="panel-caption">Notifikasi dibuat saat nilai pertama kali melewati batas dan aktif kembali setelah kondisi normal.</p>{threshold && <form className="threshold-form" onSubmit={saveThreshold}>{metrics.map(({ key, label, unit, icon: MetricIcon, step }) => <fieldset key={key}><legend><MetricIcon size={21} aria-hidden="true" />{label} <small>{unit}</small></legend><label>Minimum<input type="number" step={step} value={threshold[`${key}_min`]} onChange={(event) => setThreshold({ ...threshold, [`${key}_min`]: Number(event.target.value) })} required /></label><label>Maksimum<input type="number" step={step} value={threshold[`${key}_max`]} onChange={(event) => setThreshold({ ...threshold, [`${key}_max`]: Number(event.target.value) })} required /></label></fieldset>)}<button className="primary save" type="submit"><CheckCircle size={20} /> Simpan ambang batas</button></form>}</section>}
      {view === "notifications" && <section className="panel"><div className="panel-tools"><p className="panel-caption">{unread} belum dibaca · {alerts.length} notifikasi terakhir</p><div className="tool-actions"><button className="secondary" onClick={enablePush} disabled={pushEnabled}><Bell size={20} /> {pushEnabled ? "Push aktif" : "Aktifkan push"}</button><button className="secondary" onClick={() => markRead()} disabled={!unread}><CheckCircle size={20} /> Tandai semua dibaca</button></div></div><div className="alert-list">{alerts.map((alert) => <article key={alert.id} className={alert.read ? "read" : ""}><span className={`alert-mark ${alert.direction}`}><Bell size={20} aria-hidden="true" /></span><div><strong>{alert.device_uid} · {metrics.find((metric) => metric.key === alert.parameter)?.label || alert.parameter}</strong><p>{alert.message}</p><small>{formatDate(alert.created_at)}</small></div>{!alert.read && <button onClick={() => markRead(alert.id)}>Tandai dibaca</button>}</article>)}{!alerts.length && <p className="empty-row">Belum ada notifikasi sensor.</p>}</div></section>}
      <footer>© {new Date().getFullYear()} AquaNusa · Aerasea</footer>
    </main>
  </div>;
}

function Empty({ hasDevice, admin }: { hasDevice: boolean; admin: boolean }) {
  return <section className="empty-state"><div className="empty-icon"><Waves size={38} aria-hidden="true" /></div><h2>{hasDevice ? "Menunggu data terbaru" : "Belum ada perangkat"}</h2><p>{admin ? "Device akan muncul setelah mengirim telemetri pertama." : "Hubungi administrator bila perangkat belum terhubung ke akun Anda."}</p></section>;
}

function App() {
  const [user, setUser] = useState<User | null>(null), [checking, setChecking] = useState(true);
  useEffect(() => { api<User>("/api/v1/auth/me").then(setUser).catch(() => undefined).finally(() => setChecking(false)); }, []);
  if (checking) return <div className="app-loading" role="status"><img src="/logo.png" alt="" /><span>Memuat AquaNusa…</span></div>;
  if (!user) return <Login onLogin={setUser} />;
  return <Dashboard user={user} onLogout={() => api<void>("/api/v1/auth/logout", { method: "POST" }).finally(() => setUser(null))} />;
}

if ("serviceWorker" in navigator && import.meta.env.PROD) navigator.serviceWorker.register("/sw.js");
createRoot(document.getElementById("root")!).render(<App />);
