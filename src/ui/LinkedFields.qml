import QtQml

// Turns the commits of its number fields into one params object, keyed by each field's param. While linked, a value
// set in one field goes to every field, so dragging fade in also drags fade out, and the action runs once.
QtObject {
    id: group
    property list<QtObject> fields
    property bool linked: false

    signal committed(var params)

    function connect(field) {
        field.previewed.connect(number => {
            for (const other of group.fields) {
                if (other !== field && group.linked) {
                    other.previewValue = number
                }
            }
        })
        field.committed.connect(number => {
            const params = {}
            for (const other of group.fields) {
                other.previewValue = NaN
                if (other === field || group.linked) {
                    params[other.param] = number
                }
            }
            group.committed(params)
        })
    }

    Component.onCompleted: {
        for (const field of fields) {
            connect(field)
        }
    }
}
