import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ColumnLayout {
    id: root
    property string label:  "Value"
    property real   from:   0.0
    property real   to:     1.0
    property real   value:  0.0
    property real   stepSize: 0.01
    property string suffix:   ""



    spacing: 2

    RowLayout {
        Layout.fillWidth: true
        Text {
            text: root.label
            color: "#888888"
            font.pixelSize: 11
            Layout.fillWidth: true
        }
        Text {
            text: (root.stepSize >= 1 ? root.value.toFixed(0) : root.value.toFixed(2)) + root.suffix
            color: "#E8E8E8"
            font.pixelSize: 11
            font.family: "Menlo"
        }
    }

    Slider {
        id: slider
        Layout.fillWidth: true
        from:     root.from
        to:       root.to
        value:    root.value
        stepSize: root.stepSize

        onMoved: root.value = value

        background: Rectangle {
            x: slider.leftPadding
            y: slider.topPadding + slider.availableHeight/2 - height/2
            width:  slider.availableWidth
            height: 3
            radius: 1
            color:  "#383838"
            Rectangle {
                width:  slider.visualPosition * parent.width
                height: parent.height
                color:  "#C8A96E"
                radius: 1
            }
        }
        handle: Rectangle {
            x: slider.leftPadding
               + slider.visualPosition
               * (slider.availableWidth - width)
            y: slider.topPadding
               + slider.availableHeight/2 - height/2
            width:  14
            height: 14
            radius: 7
            color:  slider.pressed ? "#D4BB8A" : "#C8A96E"
            border.color: "#1A1A1A"
            border.width: 1
        }
    }
}
