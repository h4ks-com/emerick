import QtQuick

// The DragNumbers inside target that one press can gather, after Blender: press a field and drag up or down across the
// others to gather them, then drag sideways to move them all together. A view joins by declaring a dragGroup property
// holding one, and every DragNumber under that view finds it.
QtObject {
    required property Item target

    function fieldsCrossing(from, to) {
        const left = Math.min(from.x, to.x)
        const right = Math.max(from.x, to.x)
        const top = Math.min(from.y, to.y)
        const bottom = Math.max(from.y, to.y)
        const found = []
        const visit = item => {
            for (const child of item.children) {
                if (!child.visible) {
                    continue
                }
                if (!(child instanceof DragNumber)) {
                    visit(child)
                    continue
                }
                const corner = child.mapToItem(null, 0, 0)
                if (child.enabled && corner.x <= right && corner.x + child.width >= left && corner.y <= bottom
                        && corner.y + child.height >= top) {
                    found.push(child)
                }
            }
        }
        visit(target)
        return found
    }
}
