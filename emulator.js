// DOM Elements
const oledLines = [
    document.getElementById('oled-line1'),
    document.getElementById('oled-line2'),
    document.getElementById('oled-line3'),
    document.getElementById('oled-line4')
];
const servoStatus = document.getElementById('servo-status');
const servoText = document.getElementById('servo-text');
const iconLocked = document.querySelector('.icon-locked');
const iconUnlocked = document.querySelector('.icon-unlocked');
const ledSuccess = document.getElementById('led-success');
const ledFail = document.getElementById('led-fail');
const lineLog = document.getElementById('line-log');
const registeredList = document.getElementById('registered-list');

// Configuration
const UNLOCK_TIME_MS = 5000;
const ERROR_DISPLAY_MS = 2000;
const MAX_PASS_LENGTH = 32;

// States
const STATE_BOOT_REGISTER = 0;
const STATE_LOCKED_CARD = 1;
const STATE_LOCKED_PASS = 2;
const STATE_UNLOCKED = 3;
const STATE_ADMIN = 4;
const STATE_ADMIN_PASS1 = 5;
const STATE_ADMIN_PASS2 = 6;

let currentState = STATE_BOOT_REGISTER;
let doorUnlocked = false;
let unlockTimeout = null;
let errorTimeout = null;

// Data
let authorizedCards = [];
let savedPassword = ['UP', 'UP', 'DOWN', 'DOWN']; // Default
let currentInputPass = [];
let newPasswordBuf = [];

// Admin State
let adminCursor = 0; // 0: Action, 1: View Cards, 2: Change Pass
let adminActionMode = 0; // 0: Register, 1: Delete
let adminCardIndex = 0;

// Initialize
function init() {
    loadData();
    updateOLED("System Starting", "Please wait...", "", "");
    setTimeout(() => {
        sendLog("【スマートロック】\nESP32が起動しました。", false);
        switchState(STATE_BOOT_REGISTER);
    }, 2000);
}

// Data Management
function loadData() {
    const savedCards = localStorage.getItem('smartlock_cards');
    if (savedCards) authorizedCards = JSON.parse(savedCards);
    else authorizedCards = ['A1B2C3D4'];

    const savedPswd = localStorage.getItem('smartlock_pass');
    if (savedPswd) savedPassword = JSON.parse(savedPswd);
    
    renderDatabase();
}

function saveData() {
    localStorage.setItem('smartlock_cards', JSON.stringify(authorizedCards));
    localStorage.setItem('smartlock_pass', JSON.stringify(savedPassword));
    renderDatabase();
}

function renderDatabase() {
    registeredList.innerHTML = '';
    if(authorizedCards.length === 0) {
        registeredList.innerHTML = '<li>No cards registered.</li>';
        return;
    }
    authorizedCards.forEach(uid => {
        const li = document.createElement('li');
        li.textContent = uid;
        registeredList.appendChild(li);
    });
}

// OLED / LED Helpers
function updateOLED(l1, l2, l3, l4) {
    oledLines[0].textContent = l1 || "";
    oledLines[1].textContent = l2 || "";
    oledLines[2].textContent = l3 || "";
    oledLines[3].textContent = l4 || "";
}

function setLEDs(success, fail) {
    if (success) ledSuccess.classList.add('on');
    else ledSuccess.classList.remove('on');
    if (fail) ledFail.classList.add('on');
    else ledFail.classList.remove('on');
}

function refreshOLED() {
    switch (currentState) {
        case STATE_BOOT_REGISTER:
            updateOLED("[BOOT REGISTER]", "Scan Card to Add", "-------------", "(SW to Skip)");
            break;
        case STATE_LOCKED_CARD:
            updateOLED("[CARD UNLOCK]", "Scan RFID Card", "-------------", "(SW to Pass Mode)");
            break;
        case STATE_LOCKED_PASS:
            updateOLED("[PASS UNLOCK]", "Input sequence:", "*".repeat(currentInputPass.length), "(SW to Card Mode)");
            break;
        case STATE_UNLOCKED:
            updateOLED("UNLOCKED!", "Welcome.", "-------------", "(SW for Admin)");
            break;
        case STATE_ADMIN:
            const modeText = adminActionMode === 0 ? "< Register >" : "< Delete >";
            const cardText = authorizedCards.length > 0 ? 
                `${adminCardIndex + 1}/${authorizedCards.length} ${authorizedCards[adminCardIndex]}` : "No Cards";
            updateOLED(
                (adminCursor===0?"> ":"  ") + "Mode: " + modeText,
                (adminCursor===1?"> ":"  ") + "Card: " + cardText,
                (adminCursor===2?"> ":"  ") + "Change Password",
                (adminCursor===3?"> ":"  ") + "Exit Admin"
            );
            break;
        case STATE_ADMIN_PASS1:
            updateOLED("Enter New Pass:", "*".repeat(currentInputPass.length), "", "Press SW to confirm");
            break;
        case STATE_ADMIN_PASS2:
            updateOLED("Re-enter Pass:", "*".repeat(currentInputPass.length), "", "Press SW to verify");
            break;
    }
}

function switchState(newState) {
    if (errorTimeout) clearTimeout(errorTimeout);
    setLEDs(false, false);
    currentState = newState;
    currentInputPass = [];
    refreshOLED();
}

// Hardware Actions
function lockDoor(sendNotification = true) {
    doorUnlocked = false;
    servoStatus.classList.remove('unlocked');
    servoText.textContent = "LOCKED";
    iconLocked.classList.remove('hidden');
    iconUnlocked.classList.add('hidden');
    
    if (sendNotification) sendLog("【スマートロック】\n自動施錠しました。", false);
    switchState(STATE_LOCKED_CARD);
}

function triggerUnlock(method, detail) {
    doorUnlocked = true;
    servoStatus.classList.add('unlocked');
    servoText.textContent = "UNLOCKED";
    iconLocked.classList.add('hidden');
    iconUnlocked.classList.remove('hidden');
    setLEDs(true, false);
    
    const msg = `【スマートロック】\n認証に成功しました。\n方法: ${method}\n詳細: ${detail}\n状態: 解錠`;
    sendLog(msg, false);

    switchState(STATE_UNLOCKED);

    if (unlockTimeout) clearTimeout(unlockTimeout);
    unlockTimeout = setTimeout(() => {
        if (currentState === STATE_UNLOCKED) lockDoor(true);
    }, UNLOCK_TIME_MS);
}

function triggerError(msg1, msg2, logMsg) {
    updateOLED(msg1, msg2, "", "");
    setLEDs(false, true);
    if(logMsg) sendLog(logMsg, true);

    if (errorTimeout) clearTimeout(errorTimeout);
    errorTimeout = setTimeout(() => {
        setLEDs(false, false);
        refreshOLED();
    }, ERROR_DISPLAY_MS);
}

// Input Handlers
function pushJoystick(dir) {
    if (oledLines[0].textContent === "System Starting") return;

    if (currentState === STATE_BOOT_REGISTER) {
        if (dir === 'SW') switchState(STATE_LOCKED_CARD);
    }
    else if (currentState === STATE_LOCKED_CARD) {
        if (dir === 'SW') switchState(STATE_LOCKED_PASS);
    } 
    else if (currentState === STATE_LOCKED_PASS) {
        if (dir === 'SW') {
            // Check password
            if (currentInputPass.length === 0) {
                switchState(STATE_LOCKED_CARD);
                return;
            }
            if (JSON.stringify(currentInputPass) === JSON.stringify(savedPassword)) {
                triggerUnlock("Password", "Matched");
            } else {
                triggerError("Access Denied", "Wrong Password", "【警告】誤ったパスワード入力がありました。");
                currentInputPass = [];
            }
        } else {
            if (currentInputPass.length < MAX_PASS_LENGTH) {
                currentInputPass.push(dir);
                refreshOLED();
            }
        }
    }
    else if (currentState === STATE_UNLOCKED) {
        if (dir === 'SW') {
            if (unlockTimeout) clearTimeout(unlockTimeout);
            adminCursor = 0; adminActionMode = 0; adminCardIndex = 0;
            switchState(STATE_ADMIN);
        }
    }
    else if (currentState === STATE_ADMIN) {
        if (dir === 'UP') {
            adminCursor--;
            if (adminCursor < 0) adminCursor = 3;
            refreshOLED();
        } else if (dir === 'DOWN') {
            adminCursor++;
            if (adminCursor > 3) adminCursor = 0;
            refreshOLED();
        } else if (dir === 'LEFT' || dir === 'RIGHT') {
            if (adminCursor === 0) {
                adminActionMode = adminActionMode === 0 ? 1 : 0;
                refreshOLED();
            } else if (adminCursor === 1 && authorizedCards.length > 0) {
                if (dir === 'LEFT') adminCardIndex = (adminCardIndex - 1 + authorizedCards.length) % authorizedCards.length;
                else adminCardIndex = (adminCardIndex + 1) % authorizedCards.length;
                refreshOLED();
            }
        } else if (dir === 'SW') {
            if (adminCursor === 2) {
                switchState(STATE_ADMIN_PASS1);
            } else if (adminCursor === 3) {
                lockDoor(true);
            }
        }
    }
    else if (currentState === STATE_ADMIN_PASS1) {
        if (dir === 'SW') {
            if (currentInputPass.length > 0) {
                newPasswordBuf = [...currentInputPass];
                switchState(STATE_ADMIN_PASS2);
            }
        } else {
            if (currentInputPass.length < MAX_PASS_LENGTH) {
                currentInputPass.push(dir);
                refreshOLED();
            }
        }
    }
    else if (currentState === STATE_ADMIN_PASS2) {
        if (dir === 'SW') {
            if (JSON.stringify(currentInputPass) === JSON.stringify(newPasswordBuf)) {
                savedPassword = [...newPasswordBuf];
                saveData();
                triggerError("Success!", "Password Changed", null);
                setTimeout(() => switchState(STATE_ADMIN), 1500);
            } else {
                triggerError("Mismatch!", "Change Cancelled", null);
                setTimeout(() => switchState(STATE_ADMIN), 1500);
            }
        } else {
            if (currentInputPass.length < MAX_PASS_LENGTH) {
                currentInputPass.push(dir);
                refreshOLED();
            }
        }
    }
}

function scanCard(uid) {
    if (currentState === STATE_BOOT_REGISTER) {
        const index = authorizedCards.indexOf(uid);
        if (index >= 0) {
            triggerError("Already", "Registered", null);
            setTimeout(() => switchState(STATE_LOCKED_CARD), 2000);
        } else {
            if (authorizedCards.length < 20) {
                authorizedCards.push(uid);
                saveData();
                setLEDs(true, false);
                updateOLED("Registered!", uid, "", "");
                sendLog(`新しいカードを登録しました: ${uid}`, false);
                setTimeout(() => { setLEDs(false, false); switchState(STATE_LOCKED_CARD); }, 1500);
            } else {
                triggerError("Error", "Storage Full", null);
                setTimeout(() => switchState(STATE_LOCKED_CARD), 2000);
            }
        }
    }
    else if (currentState === STATE_LOCKED_CARD) {
        if (authorizedCards.includes(uid)) triggerUnlock("RFID", uid);
        else triggerError("Access Denied", "Unregistered", `【警告】未登録カード: ${uid}`);
    } 
    else if (currentState === STATE_ADMIN && adminCursor === 0) {
        const index = authorizedCards.indexOf(uid);
        if (adminActionMode === 0) { // Register
            if (index >= 0) {
                triggerError("Already", "Registered", null);
            } else {
                if (authorizedCards.length < 20) {
                    authorizedCards.push(uid);
                    saveData();
                    setLEDs(true, false);
                    updateOLED("Registered!", uid, "", "");
                    sendLog(`新しいカードを登録しました: ${uid}`, false);
                    setTimeout(refreshOLED, 1500);
                } else {
                    triggerError("Error", "Storage Full", null);
                }
            }
        } else { // Delete
            if (index >= 0) {
                if (authorizedCards.length <= 1) {
                    triggerError("Error", "Last Card", "【エラー】最後のカードは削除できません。");
                } else {
                    authorizedCards.splice(index, 1);
                    saveData();
                    setLEDs(true, false);
                    updateOLED("Deleted!", uid, "", "");
                    sendLog(`カードを削除しました: ${uid}`, false);
                    setTimeout(refreshOLED, 1500);
                }
            } else {
                triggerError("Not Found", uid, null);
            }
        }
    }
}

// Logger
function sendLog(message, isError) {
    const entry = document.createElement('div');
    entry.className = `log-entry ${isError ? 'error' : ''}`;
    const time = document.createElement('span');
    time.className = 'log-time';
    const now = new Date();
    time.textContent = `${now.getHours().toString().padStart(2, '0')}:${now.getMinutes().toString().padStart(2, '0')}:${now.getSeconds().toString().padStart(2, '0')}`;
    const content = document.createElement('div');
    content.textContent = message;
    entry.appendChild(time);
    entry.appendChild(content);
    lineLog.appendChild(entry);
    lineLog.scrollTop = lineLog.scrollHeight;
}

function clearLog() { lineLog.innerHTML = ''; }

window.onload = init;
