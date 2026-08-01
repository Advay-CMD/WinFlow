import { getCurrentWindow } from "@tauri-apps/api/window";

const appWindow = getCurrentWindow();
const maximizeBtn = document.getElementById("maximize");
const minimizeBtn = document.getElementById("minimize");
const closeBtn = document.getElementById("close");
const filesArea = document.querySelector(".files");
const sidebarItems = document.querySelectorAll("[data-page]");

function updateMaximizeIcon(isMaximized) {
  const icon = maximizeBtn.querySelector("svg");
  if (isMaximized) {
    icon.innerHTML =
      '<path d="M5.5 3.5H12C12.8284 3.5 13.5 4.17157 13.5 5V11H12V5.5H5.5V3.5Z" fill="none" stroke="currentColor" stroke-width="1.1"/><path d="M3.5 12.5V7C3.5 6.17157 4.17157 5.5 5 5.5H10.5C11.3284 5.5 12 6.17157 12 7V10.5C12 11.3284 11.3284 12 10.5 12H5C4.17157 12 3.5 11.3284 3.5 10.5Z" fill="none" stroke="currentColor" stroke-width="1.1"/>';
  } else {
    icon.innerHTML =
      '<rect x="3.5" y="3.5" width="9" height="9" rx="1" fill="none" stroke="currentColor" stroke-width="1.2"/>';
  }
}

async function syncMaximizeIcon() {
  const maximized = await appWindow.isMaximized();
  updateMaximizeIcon(maximized);
}

minimizeBtn.addEventListener("click", () => appWindow.minimize());
maximizeBtn.addEventListener("click", () => appWindow.toggleMaximize());
closeBtn.addEventListener("click", () => appWindow.close());

await appWindow.onResized(syncMaximizeIcon);
await syncMaximizeIcon();

async function loadPage(page) {
  try {
    const response = await fetch(`/pages/${page}`);
    if (!response.ok) throw new Error(`Failed to load ${page}`);
    filesArea.innerHTML = await response.text();
  } catch (error) {
    console.error(error);
  }
}

sidebarItems.forEach(item => {
  item.addEventListener("click", () => {
    loadPage(item.dataset.page);
  });
});

loadPage("home.html");

let isDragging = false;
let startX = 0;
let startY = 0;
let box = null;
let removeTimer = null;

function getLocalPoint(e) {
    const rect = filesArea.getBoundingClientRect();

    return {
        x: e.clientX - rect.left + filesArea.scrollLeft,
        y: e.clientY - rect.top + filesArea.scrollTop,
    };
}

function removeBoxWithFade() {
    if (!box) return;

    box.classList.add("fade-out");

    clearTimeout(removeTimer);
    removeTimer = setTimeout(() => {
        if (box) {
            box.remove();
            box = null;
        }
    }, 120);
}

filesArea.addEventListener("pointerdown", (e) => {
    if (e.button !== 0) return;

    e.preventDefault();
    window.getSelection()?.removeAllRanges();

    const p = getLocalPoint(e);
    startX = p.x;
    startY = p.y;

    if (box) {
        box.remove();
        box = null;
    }

    isDragging = true;

    box = document.createElement("div");
    box.className = "selection-box";
    filesArea.appendChild(box);

    box.style.left = `${startX}px`;
    box.style.top = `${startY}px`;
    box.style.width = "0px";
    box.style.height = "0px";

    filesArea.setPointerCapture(e.pointerId);
});

filesArea.addEventListener("pointermove", (e) => {
    if (!isDragging || !box) return;

    const p = getLocalPoint(e);

    const left = Math.min(startX, p.x);
    const top = Math.min(startY, p.y);
    const width = Math.abs(p.x - startX);
    const height = Math.abs(p.y - startY);

    box.style.left = `${left}px`;
    box.style.top = `${top}px`;
    box.style.width = `${width}px`;
    box.style.height = `${height}px`;
});

function stopDragging() {
    isDragging = false;
    clearTimeout(removeTimer);
    removeBoxWithFade();
}

filesArea.addEventListener("pointerup", stopDragging);
filesArea.addEventListener("pointercancel", stopDragging);