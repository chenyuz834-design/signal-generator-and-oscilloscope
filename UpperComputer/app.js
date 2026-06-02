/**
 * Signal Processor – Web Oscilloscope & Signal Generator
 * Main Application (Serial + Protocol + Canvas Renderer)
 *
 * Uses Web Serial API (Chrome/Edge 89+)
 */

/* ═══════════════ Protocol Constants ═══════════════ */
const HEADER1 = 0xAA, HEADER2 = 0x55;
const CMD = {
    SET_WAVEFORM:   0x01,
    SET_FREQUENCY:  0x02,
    SET_AMPLITUDE:  0x03,
    SIG_ONOFF:      0x04,
    SET_SAMPLERATE: 0x10,
    OSC_ONOFF:      0x11,
    WAVE_DATA:      0x80,
    PARAM_ACK:      0x81,
    ERROR:          0xFF,
};
const VREF = 3.3;  // DAC/ADC reference voltage
const ADC_MAX = 4095;
const MAX_FRAME_SAMPLES = 2048;
const SIGGEN_MAX_FREQ = 5000;

/* ═══════════════ Protocol Encoder / Decoder ═══════════════ */
function buildFrame(cmd, data = []) {
    const len = data.length;
    const buf = new Uint8Array(2 + 1 + 2 + len + 1);
    buf[0] = HEADER1;
    buf[1] = HEADER2;
    buf[2] = cmd;
    buf[3] = (len >> 8) & 0xFF;
    buf[4] = len & 0xFF;
    for (let i = 0; i < len; i++) buf[5 + i] = data[i];
    let xor = 0;
    for (let i = 2; i < 5 + len; i++) xor ^= buf[i];
    buf[5 + len] = xor;
    return buf;
}

function uint32BE(v) {
    return [(v >> 24) & 0xFF, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF];
}
function uint16BE(v) {
    return [(v >> 8) & 0xFF, v & 0xFF];
}

const delay = ms => new Promise(resolve => setTimeout(resolve, ms));

function clampSignalFreq(value) {
    const freq = parseInt(value, 10);
    if (!Number.isFinite(freq)) return 1000;
    return Math.min(Math.max(freq, 1), SIGGEN_MAX_FREQ);
}

async function sendFrameRepeat(cmd, data = [], times = 3, gapMs = 25) {
    for (let i = 0; i < times; i++) {
        await sendFrame(cmd, data);
        if (i + 1 < times) await delay(gapMs);
    }
}

/* Frame parser (state machine) */
class FrameParser {
    constructor(onFrame) {
        this.onFrame = onFrame;
        this.reset();
    }
    reset() {
        this.state = 0; // 0=IDLE 1=H2 2=CMD 3=LEN_H 4=LEN_L 5=DATA 6=XOR
        this.cmd = 0; this.len = 0; this.idx = 0; this.xor = 0; this.data = [];
    }
    feed(bytes) {
        for (const b of bytes) {
            switch (this.state) {
                case 0: if (b === HEADER1) this.state = 1; break;
                case 1: this.state = b === HEADER2 ? 2 : 0; break;
                case 2: this.cmd = b; this.xor = b; this.state = 3; break;
                case 3: this.len = b << 8; this.xor ^= b; this.state = 4; break;
                case 4:
                    this.len |= b; this.xor ^= b;
                    this.data = []; this.idx = 0;
                    this.state = this.len === 0 ? 6 : 5;
                    break;
                case 5:
                    this.data.push(b); this.xor ^= b;
                    if (++this.idx >= this.len) this.state = 6;
                    break;
                case 6:
                    if (b === this.xor) this.onFrame(this.cmd, this.data);
                    this.state = 0;
                    break;
            }
        }
    }
}

/* ═══════════════ Serial Connection ═══════════════ */
let port = null, reader = null, writer = null, connected = false;
let readLoop = null;

async function serialConnect() {
    if (connected) { await serialDisconnect(); return; }
    try {
        port = await navigator.serial.requestPort();
        const baud = parseInt(document.getElementById('baud-select').value);
        await port.open({ baudRate: baud, bufferSize: 8192 });
        writer = port.writable.getWriter();
        connected = true;
        updateConnectionUI(true);
        readLoop = readSerialLoop();
    } catch (e) {
        console.error('Serial connect error:', e);
        alert('串口连接失败: ' + e.message);
    }
}

async function serialDisconnect() {
    connected = false;
    try {
        if (reader) { await reader.cancel(); reader.releaseLock(); reader = null; }
        if (writer) { writer.releaseLock(); writer = null; }
        if (port)   { await port.close(); port = null; }
    } catch (e) { console.warn('Disconnect:', e); }
    updateConnectionUI(false);
}

async function readSerialLoop() {
    while (port && port.readable && connected) {
        reader = port.readable.getReader();
        try {
            while (true) {
                const { value, done } = await reader.read();
                if (done) break;
                if (value) parser.feed(value);
            }
        } catch (e) {
            if (connected) console.error('Read error:', e);
        } finally {
            reader.releaseLock();
            reader = null;
        }
    }
}

async function sendFrame(cmd, data = []) {
    if (!writer) return;
    try { await writer.write(buildFrame(cmd, data)); }
    catch (e) { console.error('Send error:', e); }
}

function updateConnectionUI(isConnected) {
    const dot = document.getElementById('status-dot');
    const txt = document.getElementById('status-text');
    dot.classList.toggle('connected', isConnected);
    txt.textContent = isConnected ? '断开连接' : '连接串口';
}

function commandName(cmd) {
    const found = Object.entries(CMD).find(([, value]) => value === cmd);
    return found ? found[0] : `0x${cmd.toString(16).padStart(2, '0')}`;
}

function updateAckUI(origCmd, status) {
    const el = document.getElementById('ack-display');
    el.textContent = `ACK ${commandName(origCmd)} ${status === 0 ? 'OK' : 'ERR'}`;
    el.classList.toggle('ok', status === 0);
    el.classList.toggle('err', status !== 0);
}

/* ═══════════════ Oscilloscope Canvas Renderer ═══════════════ */
const canvas = document.getElementById('osc-canvas');
const ctx = canvas.getContext('2d');
let waveData = [];         // latest ADC samples (0-4095)
let sampleRate = 50000;
let timePerDiv = 0.001;    // seconds
let vPerDiv = 0.5;         // volts
const GRID_DIVS_X = 10;
const GRID_DIVS_Y = 8;
const MAX_CAPTURE_SAMPLES = 200000;

function resetCapture() {
    waveData = [];
}

function appendSamples(samples) {
    const overflow = waveData.length + samples.length - MAX_CAPTURE_SAMPLES;
    if (overflow > 0) waveData.splice(0, overflow);
    for (const sample of samples) waveData.push(sample);
}

function resizeCanvas() {
    const rect = canvas.parentElement.getBoundingClientRect();
    canvas.width  = rect.width  * devicePixelRatio;
    canvas.height = rect.height * devicePixelRatio;
    canvas.style.width  = rect.width  + 'px';
    canvas.style.height = rect.height + 'px';
    ctx.setTransform(devicePixelRatio, 0, 0, devicePixelRatio, 0, 0);
}
window.addEventListener('resize', () => { resizeCanvas(); drawOscilloscope(); });

function drawOscilloscope() {
    const w = canvas.width / devicePixelRatio;
    const h = canvas.height / devicePixelRatio;

    /* Background */
    ctx.fillStyle = '#1c1c1e';
    ctx.fillRect(0, 0, w, h);

    /* Grid */
    ctx.strokeStyle = 'rgba(255,255,255,.07)';
    ctx.lineWidth = 0.5;
    for (let i = 1; i < GRID_DIVS_X; i++) {
        const x = (w / GRID_DIVS_X) * i;
        ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, h); ctx.stroke();
    }
    for (let i = 1; i < GRID_DIVS_Y; i++) {
        const y = (h / GRID_DIVS_Y) * i;
        ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke();
    }

    /* Center cross (brighter) */
    ctx.strokeStyle = 'rgba(255,255,255,.15)';
    ctx.lineWidth = 0.5;
    ctx.beginPath(); ctx.moveTo(w / 2, 0); ctx.lineTo(w / 2, h); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(0, h / 2); ctx.lineTo(w, h / 2); ctx.stroke();

    /* Graticule ticks on center lines */
    ctx.strokeStyle = 'rgba(255,255,255,.2)';
    const tickLen = 4;
    for (let i = 0; i <= GRID_DIVS_X * 5; i++) {
        const x = (w / (GRID_DIVS_X * 5)) * i;
        ctx.beginPath(); ctx.moveTo(x, h / 2 - tickLen); ctx.lineTo(x, h / 2 + tickLen); ctx.stroke();
    }
    for (let i = 0; i <= GRID_DIVS_Y * 5; i++) {
        const y = (h / (GRID_DIVS_Y * 5)) * i;
        ctx.beginPath(); ctx.moveTo(w / 2 - tickLen, y); ctx.lineTo(w / 2 + tickLen, y); ctx.stroke();
    }

    /* Waveform trace */
    if (waveData.length < 2) return;

    const totalTime = GRID_DIVS_X * timePerDiv;        // total time displayed (seconds)
    const totalVolt = GRID_DIVS_Y * vPerDiv;            // total voltage range (volts)
    const samplesPerScreen = Math.floor(totalTime * sampleRate);

    /* Use available data: if we have fewer samples than needed, show all of them;
       if we have more, only display as many as fit on screen */
    const pointCount = Math.min(samplesPerScreen, waveData.length);
    if (pointCount < 2) return;
    const startIdx = waveData.length - pointCount;

    /* Calculate pixel step - spread points evenly across canvas width
       when we have fewer samples than screen needs */
    const xScale = (pointCount < samplesPerScreen)
        ? w / (pointCount - 1)              // stretch available data to fill screen
        : w / (samplesPerScreen - 1);       // normal: 1 sample per pixel-step

    ctx.save();
    ctx.strokeStyle = '#30d158';
    ctx.lineWidth = 2;
    ctx.shadowColor = '#30d158';
    ctx.shadowBlur = 4;
    ctx.beginPath();

    for (let i = 0; i < pointCount; i++) {
        const x = i * xScale;
        const voltage = (waveData[startIdx + i] / ADC_MAX) * VREF;
        const y = h - ((voltage / totalVolt) * h);

        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
    }
    ctx.stroke();
    ctx.restore();

    /* Info overlays */
    document.getElementById('osc-info-tl').textContent =
        `${formatTime(timePerDiv)}/div  ${sampleRate >= 1e6 ? (sampleRate/1e6).toFixed(1)+' MSPS' : (sampleRate/1e3).toFixed(0)+' kSPS'}`;
    document.getElementById('osc-info-tr').textContent =
        `${formatVoltage(vPerDiv)}/div`;
}

/* ═══════════════ Measurements ═══════════════ */
function updateMeasurements() {
    if (waveData.length < 10) return;

    const samplesForMeasure = waveData.slice(-Math.min(waveData.length, 5000));
    const voltages = samplesForMeasure.map(v => (v / ADC_MAX) * VREF);
    const vmax = Math.max(...voltages);
    const vmin = Math.min(...voltages);
    const vpp = vmax - vmin;
    const vavg = voltages.reduce((a, b) => a + b, 0) / voltages.length;

    /* Simple frequency estimation: count zero-crossings (AC coupled) */
    let crossings = 0;
    for (let i = 1; i < voltages.length; i++) {
        if ((voltages[i - 1] - vavg) * (voltages[i] - vavg) < 0) crossings++;
    }
    const totalTime = samplesForMeasure.length / sampleRate;
    const freq = crossings > 0 ? crossings / (2 * totalTime) : 0;

    document.getElementById('m-freq').textContent = freq > 0 ? formatFreq(freq) : '-- Hz';
    document.getElementById('m-vpp').textContent  = vpp.toFixed(2) + ' V';
    document.getElementById('m-vmax').textContent  = vmax.toFixed(2) + ' V';
    document.getElementById('m-vmin').textContent  = vmin.toFixed(2) + ' V';
    document.getElementById('m-vavg').textContent  = vavg.toFixed(2) + ' V';
}

/* ═══════════════ Formatting Helpers ═══════════════ */
function formatTime(s) {
    if (s >= 1) return s.toFixed(0) + ' s';
    if (s >= 0.001) return (s * 1e3).toFixed(0) + ' ms';
    return (s * 1e6).toFixed(0) + ' µs';
}
function formatVoltage(v) {
    if (v >= 1) return v.toFixed(1) + ' V';
    return (v * 1000).toFixed(0) + ' mV';
}
function formatFreq(f) {
    if (f >= 1e6) return (f / 1e6).toFixed(2) + ' MHz';
    if (f >= 1e3) return (f / 1e3).toFixed(2) + ' kHz';
    return f.toFixed(1) + ' Hz';
}

/* ═══════════════ Frame Handler ═══════════════ */
let frameCount = 0, lastFpsTime = performance.now();

function handleFrame(cmd, data) {
    if (cmd === CMD.WAVE_DATA && data.length >= 4) {
        const count = (data[0] << 8) | data[1];
        const expectedLen = 2 + count * 2;
        if (count === 0 || count > MAX_FRAME_SAMPLES || data.length !== expectedLen) return;

        const samples = new Uint16Array(count);
        for (let i = 0; i < count; i++) {
            const sample = (data[2 + i * 2] << 8) | data[2 + i * 2 + 1];
            if (sample > ADC_MAX) return;
            samples[i] = sample;
        }
        appendSamples(samples);
        drawOscilloscope();
        updateMeasurements();

        frameCount++;
        const now = performance.now();
        if (now - lastFpsTime >= 1000) {
            document.getElementById('fps-display').textContent = frameCount + ' FPS';
            frameCount = 0;
            lastFpsTime = now;
        }
    } else if (cmd === CMD.PARAM_ACK) {
        if (data.length >= 2) updateAckUI(data[0], data[1]);
    }
}

const parser = new FrameParser(handleFrame);

/* ═══════════════ UI Event Bindings ═══════════════ */
document.addEventListener('DOMContentLoaded', () => {
    resizeCanvas();
    drawOscilloscope();

    /* Serial connect */
    document.getElementById('btn-connect').addEventListener('click', serialConnect);

    /* ── Signal Generator ── */
    let sigRunning = false;

    document.querySelectorAll('.btn-wave').forEach(btn => {
        btn.addEventListener('click', () => {
            document.querySelectorAll('.btn-wave').forEach(b => b.classList.remove('active'));
            btn.classList.add('active');
            resetCapture();
            sendFrame(CMD.SET_WAVEFORM, [parseInt(btn.dataset.wave)]);
        });
    });

    const freqInput  = document.getElementById('sig-freq');
    const freqSlider = document.getElementById('sig-freq-slider');
    const ampInput   = document.getElementById('sig-amp');
    const ampSlider  = document.getElementById('sig-amp-slider');

    freqInput.addEventListener('change', () => {
        const freq = clampSignalFreq(freqInput.value);
        freqInput.value = freq;
        freqSlider.value = freq;
        resetCapture();
        sendFrame(CMD.SET_FREQUENCY, uint32BE(freq));
    });
    freqSlider.addEventListener('input', () => {
        const freq = clampSignalFreq(freqSlider.value);
        freqInput.value = freq;
        freqSlider.value = freq;
        resetCapture();
        sendFrame(CMD.SET_FREQUENCY, uint32BE(freq));
    });
    ampInput.addEventListener('change', () => {
        ampSlider.value = ampInput.value;
        resetCapture();
        sendFrame(CMD.SET_AMPLITUDE, uint16BE(parseInt(ampInput.value)));
    });
    ampSlider.addEventListener('input', () => {
        ampInput.value = ampSlider.value;
        resetCapture();
        sendFrame(CMD.SET_AMPLITUDE, uint16BE(parseInt(ampSlider.value)));
    });

    document.getElementById('btn-sig-toggle').addEventListener('click', async function () {
        sigRunning = !sigRunning;
        resetCapture();
        if (sigRunning) {
            /* Send all parameters first, then start */
            const activeWave = document.querySelector('.btn-wave.active');
            const waveType = activeWave ? parseInt(activeWave.dataset.wave) : 0;
            await sendFrame(CMD.SET_WAVEFORM, [waveType]);
            await delay(30);
            await sendFrame(CMD.SET_FREQUENCY, uint32BE(clampSignalFreq(freqInput.value)));
            await delay(30);
            await sendFrame(CMD.SET_AMPLITUDE, uint16BE(parseInt(ampInput.value) || 3300));
            await delay(30);
            await sendFrameRepeat(CMD.SIG_ONOFF, [1], 2);
        } else {
            await sendFrameRepeat(CMD.SIG_ONOFF, [0], 4);
        }
        this.classList.toggle('running', sigRunning);
        this.textContent = sigRunning ? '⏹ 停止信号源' : '▶ 启动信号源';
    });

    /* ── Oscilloscope Controls ── */
    let oscRunning = false;

    document.getElementById('osc-sps').addEventListener('change', function () {
        sampleRate = Math.min(parseInt(this.value), 50000);
        resetCapture();
        sendFrame(CMD.SET_SAMPLERATE, uint32BE(sampleRate));
    });
    document.getElementById('osc-timebase').addEventListener('change', function () {
        timePerDiv = parseFloat(this.value);
        drawOscilloscope();
    });
    document.getElementById('osc-vdiv').addEventListener('change', function () {
        vPerDiv = parseFloat(this.value);
        drawOscilloscope();
    });

    document.getElementById('btn-osc-toggle').addEventListener('click', async function () {
        oscRunning = !oscRunning;
        if (oscRunning) {
            const sps = parseInt(document.getElementById('osc-sps').value);
            sampleRate = Math.min(sps, 50000);
            resetCapture();
            await sendFrame(CMD.SET_SAMPLERATE, uint32BE(sampleRate));
            await delay(30);
            await sendFrame(CMD.OSC_ONOFF, [1]);
        } else {
            await sendFrame(CMD.OSC_ONOFF, [0]);
        }
        this.classList.toggle('running', oscRunning);
        this.textContent = oscRunning ? '⏹ 停止示波器' : '▶ 启动示波器';
    });

    /* Check Web Serial support */
    if (!('serial' in navigator)) {
        alert('您的浏览器不支持 Web Serial API。\n请使用 Chrome 89+ 或 Edge 89+ 浏览器。');
    }
});
