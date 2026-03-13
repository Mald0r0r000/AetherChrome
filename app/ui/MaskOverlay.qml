import QtQuick
import QtQuick.Controls

Item {
    id: root
    anchors.fill: parent

    property bool maskModeActive: false
    property bool negativeMode: false

    // Canvas for drawing prompt points
    Canvas {
        id: promptCanvas
        anchors.fill: parent
        visible: maskModeActive

        property var positivePoints: []
        property var negativePoints: []

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            
            // Draw positive points (Gold)
            positivePoints.forEach(p => {
                ctx.beginPath()
                ctx.arc(p.x * width, p.y * height, 8, 0, 2*Math.PI)
                ctx.fillStyle = "#C8A96E"
                ctx.fill()
                ctx.strokeStyle = "white"
                ctx.lineWidth = 2
                ctx.stroke()
            })

            // Draw negative points (Red)
            negativePoints.forEach(p => {
                ctx.beginPath()
                ctx.arc(p.x * width, p.y * height, 8, 0, 2*Math.PI)
                ctx.fillStyle = "#E05555"
                ctx.fill()
                ctx.strokeStyle = "white"
                ctx.lineWidth = 2
                ctx.stroke()
            })
        }

        function clear() {
            positivePoints = []
            negativePoints = []
            requestPaint()
        }
    }

    // Interaction handler
    TapHandler {
        enabled: maskModeActive
        onTapped: (eventPoint) => {
            var normX = eventPoint.position.x / parent.width
            var normY = eventPoint.position.y / parent.height
            var isPositive = !negativeMode
            
            pipeline.segmentAtPoint(normX, normY, isPositive)
            
            if (isPositive)
                promptCanvas.positivePoints.push({x: normX, y: normY})
            else
                promptCanvas.negativePoints.push({x: normX, y: normY})
            
            promptCanvas.requestPaint()
        }
    }

    // AI Control Sidebar
    Column {
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 12
        visible: pipeline.aiAvailable

        Repeater {
            model: [
                { name: "Person", color: "#2E2E2E" },
                { name: "Garment", color: "#2E2E2E" },
                { name: "Background", color: "#2E2E2E" }
            ]
            delegate: Button {
                text: modelData.name
                width: 100
                background: Rectangle {
                    color: modelData.color
                    radius: 4
                    border.color: "#C8A96E"
                    border.width: 1
                }
                contentItem: Text {
                    text: parent.text
                    color: "white"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                onClicked: {
                    promptCanvas.clear()
                    pipeline.segmentSubject(index)
                }
            }
        }

        Button {
            text: "Clear"
            width: 100
            onClicked: {
                promptCanvas.clear()
                pipeline.clearAIMasks()
            }
            background: Rectangle {
                color: "#3A2020"
                radius: 4
            }
            contentItem: Text {
                text: parent.text
                color: "#E05555"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
         
        // Toggle mask mode button
        Button {
            onClicked: {
                maskModeActive = !maskModeActive
                if (maskModeActive) {
                    pipeline.prepareAI()
                }
            }
            background: Rectangle {
                color: maskModeActive ? "#C8A96E" : "#1A1A1A"
                radius: 4
                border.color: "#C8A96E"
            }
            contentItem: Text {
                text: parent.text
                color: maskModeActive ? "black" : "white"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    // Encoding Indicator
    Rectangle {
        visible: pipeline.aiEncoding
        anchors.top: parent.top
        anchors.topMargin: 15
        anchors.horizontalCenter: parent.horizontalCenter
        width: 200; height: 30
        color: "#AA1A1A1A"
        radius: 15
        border.color: "#C8A96E"
        Text {
            anchors.centerIn: parent
            text: "AI Encoding image..."
            color: "#C8A96E"
            font.pixelSize: 12
        }
    }

    Connections {
        target: pipeline
        function onAiMaskReady() {
            promptCanvas.requestPaint()
        }
    }
}
