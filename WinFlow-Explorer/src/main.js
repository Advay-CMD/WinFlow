import { getCurrentWindow } from "@tauri-apps/api/window";

const appWindow = getCurrentWindow();
const maximizeBtn = document.getElementById("maximize");
const minimizeBtn = document.getElementById("minimize");
const closeBtn = document.getElementById("close");

const filesArea = document.querySelector(".files");
const sidebarItems = document.querySelectorAll("[data-page]");


// ===============================
// WINDOW CONTROLS
// ===============================

function updateMaximizeIcon(isMaximized) {
    const icon = maximizeBtn.querySelector("svg");

    if (isMaximized) {
        icon.innerHTML =
            '<path d="M5.5 3.5H12C12.8284 3.5 13.5 4.17157 13.5 5V11H12V5.5H5.5V3.5Z" fill="none" stroke="currentColor" stroke-width="1.1"/>' +
            '<path d="M3.5 12.5V7C3.5 6.17157 4.17157 5.5 5 5.5H10.5C11.3284 5.5 12 6.17157 12 7V10.5C12 11.3284 11.3284 12 10.5 12H5C4.17157 12 3.5 11.8284 3.5 10.5Z" fill="none" stroke="currentColor" stroke-width="1.1"/>';
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


// ===============================
// PAGE LOADING
// ===============================

async function loadPage(page) {
    try {
        const response = await fetch(`/pages/${page}`);

        if (!response.ok) {
            throw new Error(`Failed to load ${page}`);
        }

        filesArea.innerHTML = await response.text();

        // Clear old selections whenever changing pages
        clearSelectedCards();

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


// ===============================
// SELECTION BOX
// ===============================

let isDragging = false;

let startX = 0;
let startY = 0;

let box = null;
let removeTimer = null;


// ===============================
// GET MOUSE POSITION
// ===============================

function getLocalPoint(e) {
    const rect = filesArea.getBoundingClientRect();

    return {
        x: e.clientX - rect.left + filesArea.scrollLeft,
        y: e.clientY - rect.top + filesArea.scrollTop,
    };
}


// ===============================
// CLEAR ALL SELECTIONS
// ===============================

function clearSelected() {
    filesArea
        .querySelectorAll(".selectable.selected")
        .forEach(element => {
            element.classList.remove("selected");
        });
}


// ===============================
// CHECK IF TWO RECTANGLES OVERLAP
// ===============================

function rectanglesOverlap(a, b) {
    return (
        a.left < b.right &&
        a.right > b.left &&
        a.top < b.bottom &&
        a.bottom > b.top
    );
}


// ===============================
// UPDATE SELECTED ELEMENTS
// ===============================

function updateSelection() {
    if (!box) return;

    const boxRect = box.getBoundingClientRect();

    filesArea
        .querySelectorAll(".selectable")
        .forEach(element => {

            const elementRect =
                element.getBoundingClientRect();

            const selected =
                rectanglesOverlap(boxRect, elementRect);

            element.classList.toggle(
                "selected",
                selected
            );
        });
}


// ===============================
// FADE OUT SELECTION BOX
// ===============================

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


// ===============================
// POINTER DOWN
// ===============================

filesArea.addEventListener("pointerdown", (e) => {

    if (e.button !== 0) return;

    /*
     * If clicking directly on a selectable item,
     * don't start a selection rectangle.
     */
    if (e.target.closest(".selectable")) {
        return;
    }

    e.preventDefault();

    // Prevent text highlighting
    window.getSelection()?.removeAllRanges();

    const p = getLocalPoint(e);

    startX = p.x;
    startY = p.y;


    // Remove old selection rectangle
    if (box) {
        box.remove();
        box = null;
    }

    clearTimeout(removeTimer);


    // Remove previous selections
    clearSelected();


    isDragging = true;


    // Create rectangle
    box = document.createElement("div");

    box.className = "selection-box";

    filesArea.appendChild(box);


    box.style.left = `${startX}px`;
    box.style.top = `${startY}px`;

    box.style.width = "0px";
    box.style.height = "0px";


    // Keep receiving pointer events
    filesArea.setPointerCapture(e.pointerId);
});


// ===============================
// POINTER MOVE
// ===============================

filesArea.addEventListener("pointermove", (e) => {

    if (!isDragging || !box) return;


    const p = getLocalPoint(e);


    const left =
        Math.min(startX, p.x);

    const top =
        Math.min(startY, p.y);

    const width =
        Math.abs(p.x - startX);

    const height =
        Math.abs(p.y - startY);


    box.style.left =
        `${left}px`;

    box.style.top =
        `${top}px`;

    box.style.width =
        `${width}px`;

    box.style.height =
        `${height}px`;


    // Check every .selectable element
    updateSelection();
});


// ===============================
// STOP DRAGGING
// ===============================

function stopDragging(e) {

    if (!isDragging) return;

    isDragging = false;


    if (
        e &&
        e.pointerId !== undefined &&
        filesArea.hasPointerCapture(e.pointerId)
    ) {
        filesArea.releasePointerCapture(
            e.pointerId
        );
    }


    clearTimeout(removeTimer);

    removeBoxWithFade();
}


// ===============================
// POINTER UP / CANCEL
// ===============================

filesArea.addEventListener(
    "pointerup",
    stopDragging
);

filesArea.addEventListener(
    "pointercancel",
    stopDragging
);