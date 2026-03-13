import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: root
    visible: true
    width:  1440
    height: 900
    minimumWidth:  1024
    minimumHeight: 700
    title: "AetherChrome" +
           (pipeline && pipeline.ready ? " — " + pipeline.cameraInfo : "")
    color: "#1A1A1A"

    // ── Toolbar ─────────────────────────────────────────────
    header: Rectangle {
        height: 44
        color:  "#242424"
        border.color: "#383838"
        border.width: 1

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin:  12
            anchors.rightMargin: 12
            spacing: 8

            // Logo / Nom
            Text {
                text: "AetherChrome"
                color: "#C8A96E"
                font.pixelSize: 15
                font.weight: Font.Medium
            }

            // Bouton Open
            Rectangle {
                width: 90; height: 28
                color: openHover.containsMouse
                       ? "#3A3A3A" : "#2E2E2E"
                radius: 5
                border.color: "#C8A96E"
                border.width: 1
                Text {
                    anchors.centerIn: parent
                    text: "Open RAW"
                    color: "#C8A96E"
                    font.pixelSize: 12
                }
                HoverHandler { id: openHover }
                TapHandler {
                    onTapped: fileDialog.open()
                }
            }

            Item { Layout.fillWidth: true }

            Text {
                visible: pipeline && pipeline.ready
                text: pipeline ? pipeline.cameraInfo : ""
                color: "#888888"
                font.pixelSize: 11
                font.family: "Menlo"
            }

            // Bouton Export
            Rectangle {
                visible: pipeline && pipeline.ready
                width: 70; height: 28
                color: exportHover.containsMouse
                       ? "#3A3A3A" : "#2E2E2E"
                radius: 5
                border.color: "#383838"
                border.width: 1
                Text {
                    anchors.centerIn: parent
                    text: "Export"
                    color: "#E8E8E8"
                    font.pixelSize: 12
                }
                HoverHandler { id: exportHover }
                TapHandler {
                    onTapped: exportDialog.open()
                }
            }
        }
    }

    // ── Layout principal ────────────────────────────────────
    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Zone image (flexible)
        Viewer {
            id: viewer
            Layout.fillWidth:  true
            Layout.fillHeight: true
        }

        // Séparateur vertical
        Rectangle {
            width: 1
            Layout.fillHeight: true
            color: "#383838"
        }

        // Panneau de contrôle (fixe 280px)
        ControlPanel {
            width: 280
            Layout.fillHeight: true
        }
    }

    // ── Film strip bas ──────────────────────────────────────
    footer: FilmStrip {
        width: parent.width
        height: 80
    }

    // ── File dialogs ────────────────────────────────────────
    FileDialog {
        id: fileDialog
        title: "Open RAW File"
        nameFilters: [
            "RAW files (*.arw *.cr2 *.cr3 *.nef *.orf " +
                       "*.rw2 *.dng *.raf *.pef *.srw)",
            "All files (*)"
        ]
        onAccepted: pipeline.loadFile(
            selectedFile.toString().replace("file://", ""))
    }

    FileDialog {
        id: exportDialog
        title: "Export Image"
        fileMode: FileDialog.SaveFile
        nameFilters: ["TIFF (*.tiff *.tif)", "JPEG (*.jpg)"]
        onAccepted: pipeline.exportFullRes(
            selectedFile.toString().replace("file://", ""))
    }
}
