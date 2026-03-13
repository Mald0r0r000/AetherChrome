import QtQuick
import QtQuick.Controls

Item {
    id: viewer
    clip: true

    // Zone de drop
    DropArea {
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: (drop) => {
            var path = drop.urls[0].toString()
                           .replace("file://", "")
            pipeline.loadFile(path)
        }
    }

    // Image preview
    Item {
        id: imageContainer
        anchors.fill: parent

        Image {
            id: previewImage
            anchors.centerIn: parent
            width:  Math.min(parent.width,
                    parent.height * sourceSize.width
                                  / Math.max(sourceSize.height, 1))
            height: Math.min(parent.height,
                    parent.width  * sourceSize.height
                                  / Math.max(sourceSize.width, 1))
            source:      ""
            cache:       false
            asynchronous: true
            smooth:      true
            fillMode:    Image.Stretch

            // Zoom + pan via transform
            transform: [
                Scale {
                    id: imgScale
                    origin.x: previewImage.width  / 2
                    origin.y: previewImage.height / 2
                    xScale: imageContainer.zoomLevel
                    yScale: imageContainer.zoomLevel
                },
                Translate {
                    id: imgTranslate
                    x: imageContainer.panX
                    y: imageContainer.panY
                }
            ]
        }

        property real zoomLevel: 1.0
        property real panX: 0.0
        property real panY: 0.0

        // Zoom molette
        WheelHandler {
            onWheel: (event) => {
                var delta = event.angleDelta.y / 120.0
                var factor = 1.0 + delta * 0.12
                imageContainer.zoomLevel = Math.max(0.1,
                    Math.min(16.0,
                    imageContainer.zoomLevel * factor))
            }
        }

        // Pan drag
        DragHandler {
            onTranslationChanged: (delta) => {
                imageContainer.panX += delta.x
                imageContainer.panY += delta.y
            }
        }

        // Double-clic : reset zoom
        TapHandler {
            onDoubleTapped: {
                imageContainer.zoomLevel = 1.0
                imageContainer.panX = 0
                imageContainer.panY = 0
            }
        }
    }

    // Placeholder "Drop RAW here"
    Column {
        anchors.centerIn: parent
        visible: !pipeline.ready
        spacing: 12

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "⌘"
            color: "#383838"
            font.pixelSize: 48
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Drop a RAW file or click Open"
            color: "#555555"
            font.pixelSize: 16
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "ARW · CR2 · CR3 · NEF · DNG · RAF"
            color: "#383838"
            font.pixelSize: 12
            font.family: "Menlo"
        }
    }

    // Spinner pendant le calcul preview
    Rectangle {
        anchors.centerIn: parent
        visible: pipeline.ready && previewImage.status
                 === Image.Loading
        color: "#CC242424"
        radius: 8
        width: 120; height: 36
        Text {
            anchors.centerIn: parent
            text: "Processing…"
            color: "#C8A96E"
            font.pixelSize: 12
        }
    }

    // Refresh sur signal
    Connections {
        target: pipeline
        function onPreviewReady() {
            previewImage.source = ""
            previewImage.source =
                "image://preview/frame?" + Date.now()
        }
    }
}
