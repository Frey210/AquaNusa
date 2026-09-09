importScripts("https://www.gstatic.com/firebasejs/10.13.2/firebase-app-compat.js");
importScripts("https://www.gstatic.com/firebasejs/10.13.2/firebase-messaging-compat.js");
firebase.initializeApp({
  apiKey: "AIzaSyAtbCuJjiikuONhSAzokziJ9h3iz3yznBM",
  authDomain: "aquanusa-b9879.firebaseapp.com",
  projectId: "aquanusa-b9879",
  storageBucket: "aquanusa-b9879.firebasestorage.app",
  messagingSenderId: "843102787945",
  appId: "1:843102787945:web:b6fcab151f04ba46b005f8",
});
firebase.messaging();

const CACHE = "aquanusa-v4";
const SHELL = ["/", "/logo.png", "/manifest.webmanifest"];

self.addEventListener("install", (event) => event.waitUntil(caches.open(CACHE).then((cache) => cache.addAll(SHELL))));
self.addEventListener("activate", (event) => event.waitUntil(caches.keys().then((keys) => Promise.all(keys.filter((key) => key !== CACHE).map((key) => caches.delete(key))))));
self.addEventListener("fetch", (event) => {
  const url = new URL(event.request.url);
  if (event.request.method !== "GET" || url.origin !== self.location.origin || url.pathname.startsWith("/api/")) return;
  event.respondWith(fetch(event.request).then((response) => {
    const copy = response.clone();
    caches.open(CACHE).then((cache) => cache.put(event.request, copy));
    return response;
  }).catch(async () => {
    const cached = await caches.match(event.request);
    if (cached) return cached;
    if (event.request.mode === "navigate") return caches.match("/");
    return new Response('{"detail":"Offline"}', { status: 503, headers: { "Content-Type": "application/json" } });
  }));
});
