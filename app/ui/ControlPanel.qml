import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "components"

Rectangle {
    color: "#242424"

    ScrollView {
        anchors.fill: parent
        contentWidth: parent.width
        clip: true

        ColumnLayout {
            width: parent.width
            spacing: 0

            // ── Section LIGHT ──────────────────────────────
            SectionHeader { title: "LIGHT" }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 16
                spacing: 12

                ASlider {
                    label: "Exposure"
                    from: -5.0; to: 5.0
                    value: 0.0; stepSize: 0.01
                    Layout.fillWidth: true
                    onValueChanged: { if (pipeline) pipeline.exposure = value }
                }
                ASlider {
                    label: "Contrast"
                    from: 0.5; to: 2.0
                    value: 1.0; stepSize: 0.01
                    Layout.fillWidth: true
                    onValueChanged: { if (pipeline) pipeline.contrast = value }
                }
                ASlider {
                    label: "Highlights"
                    from: 0.0; to: 1.0
                    value: 0.0; stepSize: 0.01
                    Layout.fillWidth: true
                    onValueChanged: { if (pipeline) pipeline.highlightComp = value }
                }
                ASlider {
                    label: "Shadows"
                    from: 0.0; to: 0.5
                    value: 0.0; stepSize: 0.005
                    Layout.fillWidth: true
                    onValueChanged: { if (pipeline) pipeline.shadowLift = value }
                }
            }

            // ── Section COLOR ──────────────────────────────
            SectionHeader { title: "COLOR" }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 16
                spacing: 12

                ASlider {
                    label: "Saturation"
                    from: 0.0; to: 2.0
                    value: 1.0; stepSize: 0.01
                    Layout.fillWidth: true
                    onValueChanged: { if (pipeline) pipeline.saturation = value }
                }
                ASlider {
                    label: "WB — Tungsten ← → Daylight"
                    from: 0.0; to: 1.0
                    value: 1.0; stepSize: 0.01
                    Layout.fillWidth: true
                    onValueChanged: { if (pipeline) pipeline.illuminantBlend = value }
                }
            }

            // ── Section TONE CURVE ─────────────────────────
            SectionHeader { title: "TONE CURVE" }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 16
                spacing: 8

                Repeater {
                    model: ["Linear", "Filmic S", "ACES"]
                    RadioButton {
                        text: modelData
                        checked: (pipeline && pipeline.toneOp) === index
                        onCheckedChanged: {
                            if (checked)
                                pipeline.toneOp = index
                        }
                        contentItem: Text {
                            leftPadding: 24
                            text: parent.text
                            color: parent.checked
                                   ? "#C8A96E" : "#888888"
                            font.pixelSize: 12
                            verticalAlignment:
                                Text.AlignVCenter
                        }
                        indicator: Rectangle {
                            x: 4
                            y: parent.height/2 - height/2
                            width: 14; height: 14
                            radius: 7
                            color: "transparent"
                            border.color: parent.checked
                                ? "#C8A96E" : "#555555"
                            border.width: 2
                            Rectangle {
                                anchors.centerIn: parent
                                visible: parent.parent.checked
                                width: 6; height: 6
                                radius: 3
                                color: "#C8A96E"
                            }
                        }
                    }
                }
            }

            // ── Section INFO ───────────────────────────────
            SectionHeader { title: "INFO" }

            Text {
                Layout.margins: 16
                Layout.fillWidth: true
                visible: pipeline && pipeline.ready
                text: pipeline ? pipeline.cameraInfo : ""
                color: "#888888"
                font.pixelSize: 11
                font.family: "Menlo"
                wrapMode: Text.WordWrap
            }

            // Spacer
            Item { Layout.fillHeight: true; height: 20 }
        }
    }

    // Composant interne SectionHeader
    component SectionHeader : Rectangle {
        property string title: ""
        Layout.fillWidth: true
        height: 32
        color: "#1E1E1E"

        Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 16
            text: title
            color: "#555555"
            font.pixelSize: 10
            font.letterSpacing: 1.5
            font.weight: Font.Medium
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: "#2E2E2E"
        }
    }
}
