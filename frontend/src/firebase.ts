import { getApp, getApps, initializeApp } from "firebase/app";
import { getMessaging, getToken } from "firebase/messaging";

const app = getApps().length ? getApp() : initializeApp({
  apiKey: "AIzaSyAtbCuJjiikuONhSAzokziJ9h3iz3yznBM",
  authDomain: "aquanusa-b9879.firebaseapp.com",
  projectId: "aquanusa-b9879",
  storageBucket: "aquanusa-b9879.firebasestorage.app",
  messagingSenderId: "843102787945",
  appId: "1:843102787945:web:b6fcab151f04ba46b005f8",
});

export async function requestPushToken() {
  if (!("Notification" in window) || !("serviceWorker" in navigator)) throw new Error("Push notification tidak didukung browser ini");
  if (await Notification.requestPermission() !== "granted") throw new Error("Izin notifikasi belum diberikan");
  return getToken(getMessaging(app), {
    vapidKey: "BGugjqM784XFyS1WLOIZyAYY2fSeB3FDDNjKrHcWtDJ3LRXy-d7fqrkM7SeMLIE16Ta-MnTyRNEIchTUhqxKy78",
    serviceWorkerRegistration: await navigator.serviceWorker.ready,
  });
}
