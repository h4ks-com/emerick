import QtQuick
import Emerick.App

// One insert param as a number field. It drags across the param's range by position, so a frequency moves by ratios,
// and it shows and reads values with their unit, such as "2.5 kHz".
DragNumber {
    id: field
    required property string insertId
    required property var param
    required property real paramValue

    objectName: "insert.setParam:" + insertId + ":" + param.id
    actionId: "insert.setParam"
    toolTip: param.title
    from: 0
    to: 1
    step: Theme.paramStep
    decimals: 4
    value: position(paramValue)
    textFromValue: number => format(valueAt(number))
    valueFromText: text => position(parse(text))
    actionsFor: number => [{id: "insert.setParam", params: {insert: insertId, param: param.id, value: valueAt(number)}}]

    function position(number) {
        const low = param.minimum
        const high = param.maximum
        const clamped = Math.min(high, Math.max(low, number))
        return param.logarithmic ? Math.log(clamped / low) / Math.log(high / low) : (clamped - low) / (high - low)
    }

    function valueAt(at) {
        const low = param.minimum
        const high = param.maximum
        const number = param.logarithmic ? low * Math.pow(high / low, at) : low + at * (high - low)
        return Number(number.toPrecision(3))
    }

    function format(number) {
        if (param.unit === "Hz" && number >= 1000) {
            return (number / 1000).toFixed(number >= 10000 ? 1 : 2) + " kHz"
        }
        const digits = Math.abs(number) >= 100 || param.unit === "%" ? 0
                     : Math.abs(number) >= 10 || param.unit === ":1" ? 1 : 2
        const text = number.toFixed(digits)
        return param.unit === ":1" ? text + ":1" : param.unit ? text + " " + param.unit : text
    }

    function parse(text) {
        const kilo = /k/i.test(text)
        const number = parseFloat(text.replace(",", "."))
        return kilo ? number * 1000 : number
    }
}
