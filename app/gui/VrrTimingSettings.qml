import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.2

import StreamingPreferences 1.0

// Custom VRR timing. Each preset only chooses these four values; the
// adaptive buffer's growth and release policy is shared.
NavigableDialog {
    id: dialog
    title: qsTr("Custom VRR timing")
    standardButtons: Dialog.Close

    GridLayout {
        columns: 2
        columnSpacing: 15
        rowSpacing: 5

        Label {
            text: qsTr("Largest buffer (% of a source frame)")
            font.pointSize: 11
        }
        SpinBox {
            id: bufferSpin
            from: 25
            to: 400
            stepSize: 25
            editable: true
            value: StreamingPreferences.vrrBufferPerMille / 10
            onValueModified: StreamingPreferences.vrrBufferPerMille = value * 10
            ToolTip.delay: 1000
            ToolTip.timeout: 8000
            ToolTip.visible: hovered
            ToolTip.text: qsTr("How much delay VRR pacing may add to absorb uneven frame delivery. More buffer is smoother on unsteady networks and hosts but adds latency.")
        }

        Label {
            text: qsTr("On-time target (%)")
            font.pointSize: 11
        }
        SpinBox {
            id: targetSpin
            from: 9000
            to: 9999
            stepSize: 1
            editable: true
            value: StreamingPreferences.vrrTargetHundredths
            onValueModified: StreamingPreferences.vrrTargetHundredths = value
            textFromValue: function(value, locale) { return (value / 100).toFixed(2) }
            valueFromText: function(text, locale) { return Math.round(parseFloat(text) * 100) }
            ToolTip.delay: 1000
            ToolTip.timeout: 8000
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Share of frames that should be presented on time. The buffer grows only while late frames push presentation below this target.")
        }

        Label {
            text: qsTr("History (seconds)")
            font.pointSize: 11
        }
        SpinBox {
            id: historySpin
            from: 10
            to: 300
            stepSize: 10
            editable: true
            value: StreamingPreferences.vrrHistorySeconds
            onValueModified: StreamingPreferences.vrrHistorySeconds = value
            ToolTip.delay: 1000
            ToolTip.timeout: 8000
            ToolTip.visible: hovered
            ToolTip.text: qsTr("How long presentation quality is remembered when deciding whether to grow the buffer.")
        }

        Label {
            text: qsTr("Interval tolerance (µs)")
            font.pointSize: 11
        }
        SpinBox {
            id: toleranceSpin
            from: 250
            to: 2000
            stepSize: 250
            editable: true
            value: StreamingPreferences.vrrToleranceUs
            onValueModified: StreamingPreferences.vrrToleranceUs = value
            ToolTip.delay: 1000
            ToolTip.timeout: 8000
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Spacing error between presented frames that still counts as on time.")
        }

        Button {
            Layout.columnSpan: 2
            text: qsTr("Reset to the selected mode")
            enabled: StreamingPreferences.vrrTimingCustomized
            onClicked: StreamingPreferences.applyVrrPreset(StreamingPreferences.vrrLatencyMode)
        }
    }
}
