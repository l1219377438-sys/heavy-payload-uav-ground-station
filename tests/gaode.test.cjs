// Offline contract checks: no map API traffic and no aircraft connection.
const { test } = require('node:test');
const assert = require('node:assert/strict');
const vm = require('node:vm');
const fs = require('node:fs');
const path = require('node:path');
const html = fs.readFileSync(path.join(__dirname, '../gaode.html'), 'utf8');
const source = html.match(/<script id="gaode-app">([\s\S]*?)<\/script>/)[1];

function harness(withQt = false) {
    const elements = new Map(), scripts = [], markers = [], lines = [], intervals = [], sent = [];
    function element() {
        const classes = new Set();
        return { dataset: {}, style: { setProperty() {} }, checked: true, value: '',
            appendChild() {}, classList: { remove(c) { classes.delete(c); }, toggle(c, on) { on ? classes.add(c) : classes.delete(c); }, contains(c) { return classes.has(c); } } };
    }
    const get = id => { if (!elements.has(id)) elements.set(id, element()); return elements.get(id); };
    get('satellite').checked = false;
    class Marker {
        constructor(options) { Object.assign(this, options); markers.push(this); }
        setPosition(p) { this.position = p; }
        getPosition() { return this.position; }
        setMap(map) { this.map = map; }
    }
    class Polyline {
        constructor(options) { Object.assign(this, options); lines.push(this); }
        setPath(p) { this.path = [...p]; }
        setMap(map) { this.map = map; }
        hide() { this.hidden = true; }
        show() { this.hidden = false; }
    }
    class MapMock {
        constructor() { this.events = {}; }
        on(name, callback) { this.events[name] = callback; }
        setZoomAndCenter(zoom, center) { this.zoom = zoom; this.center = center; }
        setCenter(center) { this.center = center; }
        setFitView(overlays) { this.fitted = overlays; }
        add() {} remove() {}
    }
    let now = 0, channels = 0;
    const ctx = { document: { getElementById: get, createElement: element, head: { appendChild(script) { scripts.push(script); } } },
        Date: { now: () => now }, console, setTimeout() { return 1; }, clearTimeout() {}, setInterval(fn) { intervals.push(fn); },
        GAODE_CONFIG: { key: 'test-key' }, location: { reload() {} } };
    ctx.window = ctx;
    if (withQt) {
        ctx.qt = { webChannelTransport: {} };
        ctx.QWebChannel = function (transport, cb) { channels++; cb({ objects: { coordinateHandler: { sendCoordinates(lat, lon) { sent.push([lat, lon]); } } } }); };
    }
    vm.createContext(ctx);
    vm.runInContext(source, ctx);
    return { ctx, get, markers, lines, sent, channels: () => channels,
        advance(ms) { now += ms; intervals.forEach(fn => fn()); },
        ready() { ctx.AMap = { Map: MapMock, Marker, Polyline, TileLayer: { Satellite: class {}, RoadNet: class {} } }; scripts.find(s => s.src.includes('webapi.amap.com')).onload(); },
        click(lat, lon) { ctx.map.events.click({ lnglat: { getLat: () => lat, getLng: () => lon } }); } };
}

test('queues telemetry before SDK load and uses GCJ-02 without double conversion', () => {
    const h = harness();
    assert.equal(h.ctx.updateDronePosition(1, 39.9, 116.4, 90), true);
    assert.equal(h.markers.length, 0);
    h.ready();
    assert.equal(h.markers.length, 1);
    assert.deepEqual(Array.from(h.markers[0].position), [116.4, 39.9]);
    h.ctx.updateDronePosition(1, 39.91, 116.41, 95);
    assert.equal(h.markers.length, 1);
    assert.match(h.markers[0].content.textContent, /95.0/);
});
test('rejects invalid IDs, nonfinite data, nulls, zero fixes and swapped/out-of-range coordinates', () => {
    const h = harness(); h.ready();
    for (const args of [[0,39,116,1], [11,39,116,1], [1.5,39,116,1], [1,NaN,116,1], [1,39,Infinity,1], [1,0,0,1], [1,116,39,1], [1,null,116,1], [1,39,116,NaN]]) {
        assert.equal(h.ctx.updateDronePosition(...args), false);
    }
    assert.equal(h.markers.length, 0);
});
test('converts WGS-84 formation once and keeps simulated markers separate', () => {
    const h = harness(); h.ready();
    h.ctx.updateDronePosition(1, 39.9014035298, 116.4062427849, 100);
    h.ctx.updateDronePositionWithFormation(1, 39.9, 116.4, 3);
    assert.equal(h.markers.length, 2);
    const p = h.markers[1].position;
    assert.ok(Math.abs(p[0] - 116.4062427849) < 1e-8);
    assert.ok(Math.abs(p[1] - 39.9014035298) < 1e-8);
    assert.match(h.markers[1].content.textContent, /ΔZ 3.0/);
    h.get('clear-formation').onclick();
    assert.equal(h.markers[1].map, null);
    assert.notEqual(h.markers[0].map, null);
});
test('click bridge preserves lat/lon order and initializes once', () => {
    const h = harness(true); h.ready();
    h.click(39.9, 116.4);
    h.ctx.connectQtBridge();
    assert.deepEqual(h.sent, [[39.9, 116.4]]);
    assert.equal(h.channels(), 1);
});
test('browser preview clicks work and pending selection is sent after bridge becomes available', () => {
    const h = harness(); h.ready(); h.click(30, 120);
    assert.match(h.get('picked').textContent, /30.0000000.*120.0000000/);
    h.ctx.bridge = { sendCoordinates(lat, lon) { h.sent.push([lat, lon]); } };
    h.ctx.connectQtBridge(); h.ctx.connectQtBridge();
    assert.deepEqual(h.sent, [[30,120]]);
});
test('limits tracks, toggles visibility, clears tracks, follows selected aircraft, marks stale fixes', () => {
    const h = harness(); h.ready();
    for (let i = 0; i < 1005; i++) h.ctx.updateDronePosition(1, 30+i*.000001, 120, 1);
    assert.equal(h.lines[0].path.length, 1000);
    h.get('tracks').checked = false; h.get('tracks').onchange();
    assert.equal(h.lines[0].hidden, true);
    h.get('clear-tracks').onclick(); assert.equal(h.lines[0].path.length, 0);
    h.get('follow').value = '1'; h.get('follow').onchange();
    assert.deepEqual(h.ctx.map.center, h.markers[0].position);
    h.advance(11000); assert.equal(h.markers[0].content.classList.contains('stale'), true);
    h.ctx.updateDronePosition(1,30,120,2);
    assert.equal(h.markers[0].content.classList.contains('stale'), false);
});
test('mission replacement/clearing removes overlays; out-of-China WGS coordinates stay unchanged', () => {
    const h = harness();
    assert.equal(h.ctx.setMissionWaypoints([{lat:51.5,lon:-.1}, {lat:51.6,lon:-.2}]), true);
    h.ready();
    assert.deepEqual(Array.from(h.markers[0].position), [-.1,51.5]);
    assert.equal(h.lines.length, 1);
    assert.equal(h.ctx.setMissionWaypoints([{lat:500,lon:0}]), false);
    h.ctx.setMissionWaypoints([]);
    assert.equal(h.markers[0].map, null);
    assert.equal(h.lines[0].map, null);
});
