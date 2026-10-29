import QtQuick 2.0
import QtQuick.Controls 2.2

ItemDelegate {
    property GridView grid
    // Called with the current index when up or down cannot move further within the grid,
    // for a view that stacks several grids
    property var leaveUp: null
    property var leaveDown: null

    highlighted: grid.activeFocus && grid.currentItem === this

    Keys.onLeftPressed: {
        grid.moveCurrentIndexLeft()
    }
    Keys.onRightPressed: {
        grid.moveCurrentIndexRight()
    }
    Keys.onDownPressed: {
        var before = grid.currentIndex
        grid.moveCurrentIndexDown()
        if (leaveDown && grid.currentIndex === before) {
            leaveDown(before)
        }
    }
    Keys.onUpPressed: {
        var before = grid.currentIndex
        grid.moveCurrentIndexUp()
        if (leaveUp && grid.currentIndex === before) {
            leaveUp(before)
        }
    }
    Keys.onReturnPressed: {
        clicked()
    }
    Keys.onEnterPressed: {
        clicked()
    }
}
