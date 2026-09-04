// coordinateUtils.js
.pragma library

// 常数
const PI = 3.14159265358979324
const AXIS = 6378245.0            // 长半轴
const EE   = 0.00669342162296594323  // 偏心率平方

function outOfChina(lat, lon) {
    return lon < 72.004 || lon > 137.8347
        || lat < 0.8293   || lat > 55.8271
}

function transformLat(x, y) {
    let ret = -100.0 + 2.0*x + 3.0*y
            + 0.2*y*y + 0.1*x*y + 0.2*Math.sqrt(Math.abs(x))
    ret += (20.0*Math.sin(6.0*x*PI) + 20.0*Math.sin(2.0*x*PI)) * 2.0/3.0
    ret += (20.0*Math.sin(y*PI)       + 40.0*Math.sin(y/3.0*PI)) * 2.0/3.0
    ret += (160.0*Math.sin(y/12.0*PI)  + 320.0*Math.sin(y*PI/30.0)) * 2.0/3.0
    return ret
}

function transformLon(x, y) {
    let ret = 300.0 + x + 2.0*y
            + 0.1*x*x + 0.1*x*y + 0.1*Math.sqrt(Math.abs(x))
    ret += (20.0*Math.sin(6.0*x*PI) + 20.0*Math.sin(2.0*x*PI)) * 2.0/3.0
    ret += (20.0*Math.sin(x*PI)     + 40.0*Math.sin(x/3.0*PI)) * 2.0/3.0
    ret += (150.0*Math.sin(x/12.0*PI)+300.0*Math.sin(x/30.0*PI))*2.0/3.0
    return ret
}

// 主转换接口
function gcj02ToWgs84(gcjLat, gcjLon) {
    if (outOfChina(gcjLat, gcjLon)) {
        return { lat: gcjLat, lon: gcjLon }
    }
    let dLat = transformLat(gcjLon - 105.0, gcjLat - 35.0)
    let dLon = transformLon(gcjLon - 105.0, gcjLat - 35.0)
    let radLat = gcjLat / 180.0 * PI
    let magic = Math.sin(radLat)
    magic = 1 - EE * magic * magic
    let sqrtMagic = Math.sqrt(magic)
    dLat = (dLat * 180.0) / ((AXIS * (1 - EE)) / (magic * sqrtMagic) * PI)
    dLon = (dLon * 180.0) / (AXIS / sqrtMagic * Math.cos(radLat) * PI)
    return {
        lat: gcjLat - dLat,
        lon: gcjLon - dLon
    }
}
