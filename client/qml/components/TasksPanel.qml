import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Aura

/*
  TasksPanel — задачи и напоминания (v3).

  Недельная полоса дней с подсветкой «сегодня», цветные точки-статусы,
  прогресс «до следующего события» (ближайший remind_at, окно 24 ч)
  и добавление задач (tasks.create). Наступившие напоминания сервер
  пушит событием task.due.
*/
GlassPanel {
    id: panel

    property var allTasks: App ? App.tasks : []
    property var activeTasks: (allTasks || []).filter(function (t) { return t.status === "pending" })
    property date today: new Date()
    // Выбранный день недели (0 = Пн): -1 — все дни.
    property int selectedDay: -1

    // Задачи выбранного дня (или все активные).
    property var visibleTasks: selectedDay < 0 ? activeTasks : activeTasks.filter(function (t) {
        if (!t.remind_at) return false
        const d = new Date(String(t.remind_at).replace(" ", "T"))
        return sameDay(d, panel.today, panel.selectedDay)
    })

    // Ближайшее напоминание среди активных (для прогресс-бара).
    readonly property var nextReminder: {
        let best = null
        const list = panel.activeTasks
        for (let i = 0; i < list.length; ++i) {
            const t = list[i]
            if (!t.remind_at) continue
            const d = new Date(String(t.remind_at).replace(" ", "T"))
            if (!isNaN(d.getTime()) && d.getTime() > Date.now() &&
                (best === null || d.getTime() < best.getTime())) best = d
        }
        return best
    }
    // Доля пройденного окна (24 ч до события).
    readonly property real nextProgress: {
        if (!nextReminder) return 0
        const remain = nextReminder.getTime() - Date.now()
        const window = 24 * 60 * 60 * 1000
        if (remain <= 0) return 1
        if (remain >= window) return 0
        return 1 - remain / window
    }

    function sameDay(date, ref, targetWd) {
        // targetWd: 0=Пн…6=Вс в текущей неделе ref.
        const first = new Date(ref)
        const shift = (first.getDay() + 6) % 7  // Пн=0
        first.setDate(ref.getDate() - shift + targetWd)
        return date.getFullYear() === first.getFullYear()
                && date.getMonth() === first.getMonth()
                && date.getDate() === first.getDate()
    }

    function dayLabel(i) {
        return ["Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс"][i]
    }
    function dayNumber(i) {
        const first = new Date(panel.today)
        const shift = (first.getDay() + 6) % 7
        first.setDate(panel.today.getDate() - shift + i)
        return first.getDate()
    }
    // Есть ли задача в дне i (для точки под днём). Список передаётся
    // аргументом, чтобы биндинг отслеживал зависимость от activeTasks.
    function dayHasTasks(i, list) {
        for (let k = 0; k < list.length; ++k) {
            if (!list[k].remind_at) continue
            const d = new Date(String(list[k].remind_at).replace(" ", "T"))
            if (!isNaN(d.getTime()) && sameDay(d, panel.today, i)) return true
        }
        return false
    }

    padding: AuraTheme.spaceMd

    ColumnLayout {
        anchors.fill: parent
        spacing: AuraTheme.spaceSm

        // ------------------------------------------------------------ шапка
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: "Задачи и напоминания"
                font.family: AuraTheme.fontFamily
                font.pixelSize: AuraTheme.fontSmall
                font.weight: Font.DemiBold
                color: AuraTheme.textSecondary
            }
            Item { Layout.fillWidth: true }
            Text {
                text: panel.activeTasks.length + " активных"
                font.family: AuraTheme.fontFamily
                font.pixelSize: AuraTheme.fontMicro
                color: AuraTheme.textMuted
            }
        }

        // ------------------------------------- недельная полоса дней (v3)
        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            Repeater {
                model: 7
                delegate: Rectangle {
                    required property int index
                    readonly property bool isToday: {
                        const shift = (panel.today.getDay() + 6) % 7
                        return index === shift
                    }
                    readonly property bool isSelected: panel.selectedDay === index
                    Layout.fillWidth: true
                    Layout.preferredHeight: 46
                    radius: AuraTheme.radiusField
                    color: isSelected ? AuraTheme.accentA(0.18)
                                      : (isToday ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(1, 1, 1, 0.04))
                    border.width: 1
                    border.color: isSelected ? AuraTheme.accentA(0.45)
                                             : Qt.rgba(1, 1, 1, AuraTheme.glassBorder)

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 1
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: panel.dayLabel(index)
                            font.family: AuraTheme.fontFamily
                            font.pixelSize: 9
                            color: isSelected || isToday ? AuraTheme.lilac : AuraTheme.textMuted
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: panel.dayNumber(index)
                            font.family: AuraTheme.fontFamily
                            font.pixelSize: 12
                            font.weight: isToday ? Font.Bold : Font.Normal
                            color: isSelected || isToday ? AuraTheme.textPrimary : AuraTheme.textSecondary
                        }
                    }

                    // Точка-индикатор задач в этот день
                    Rectangle {
                        visible: panel.dayHasTasks(index, panel.activeTasks)
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 4
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 4
                        height: 4
                        radius: 2
                        color: AuraTheme.accent
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: panel.selectedDay = panel.selectedDay === index ? -1 : index
                    }
                }
            }
        }

        // ------------------------------- прогресс «до следующего события»
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            visible: panel.nextReminder !== null

            Text {
                Layout.fillWidth: true
                text: {
                    if (!panel.nextReminder) return ""
                    const mins = Math.round((panel.nextReminder.getTime() - Date.now()) / 60000)
                    const h = Math.floor(mins / 60)
                    const m = mins % 60
                    return h > 0 ? "До следующего события: " + h + " ч " + m + " мин"
                                 : "До следующего события: " + m + " мин"
                }
                font.family: AuraTheme.fontFamily
                font.pixelSize: AuraTheme.fontMicro
                color: AuraTheme.textSecondary
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 6
                radius: 3
                color: Qt.rgba(1, 1, 1, 0.08)

                Rectangle {
                    width: Math.max(6, parent.width * panel.nextProgress)
                    height: parent.height
                    radius: 3
                    // Градиентный заполнитель как CTA (v3)
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: AuraTheme.accent }
                        GradientStop { position: 1.0; color: AuraTheme.accentSoft }
                    }
                    Behavior on width { NumberAnimation { duration: AuraTheme.slowMs } }
                }
            }
        }

        // ------------------------------------------------------- добавление
        RowLayout {
            Layout.fillWidth: true
            spacing: AuraTheme.spaceSm

            GlassField {
                id: titleField
                Layout.fillWidth: true
                implicitHeight: 42
                placeholderText: "новая задача…"
                onAccepted: addButton.clicked()
            }
            GlassField {
                id: remindField
                Layout.preferredWidth: 200
                implicitHeight: 42
                placeholderText: "напомнить: 2026-09-21 10:00"
                onAccepted: addButton.clicked()
            }
            // FAB «+» (v3): акцентная круглая кнопка
            GlassButton {
                id: addButton
                variant: "accent"
                text: ""
                glyph: "＋"
                implicitWidth: 46
                implicitHeight: 42
                enabled: titleField.text.trim().length > 0
                onClicked: {
                    // Сервер ждёт ISO-метку: заменяем пробел на «T».
                    var remind = remindField.text.trim().replace(" ", "T")
                    App.createTask(titleField.text, "", remind)
                    titleField.text = ""
                    remindField.text = ""
                }
            }
        }

        // ----------------------------------------------------------- список
        ListView {
            id: taskList
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 220)
            model: panel.visibleTasks
            clip: true
            spacing: 6
            boundsBehavior: Flickable.StopAtBounds

            Text {
                anchors.centerIn: parent
                visible: taskList.count === 0
                text: panel.selectedDay < 0
                      ? "Пока пусто — Аура создаёт задачи из напоминаний"
                      : "В этот день задач нет"
                font.family: AuraTheme.fontFamily
                font.pixelSize: AuraTheme.fontMicro
                color: AuraTheme.textMuted
            }

            delegate: RowLayout {
                id: taskRow
                required property var modelData
                width: taskList.width
                spacing: AuraTheme.spaceSm

                // Цветная точка-статус (v3): скоро — жёлтая, обычная — фиолетовая
                Rectangle {
                    Layout.preferredWidth: 8
                    Layout.preferredHeight: 8
                    radius: 4
                    color: {
                        if (!taskRow.modelData.remind_at) return AuraTheme.accent
                        const d = new Date(String(taskRow.modelData.remind_at).replace(" ", "T"))
                        if (!isNaN(d.getTime()) && d.getTime() - Date.now() < 24 * 3600 * 1000)
                            return AuraTheme.warning
                        return AuraTheme.accent
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text {
                        Layout.fillWidth: true
                        text: taskRow.modelData.title
                        elide: Text.ElideRight
                        font.family: AuraTheme.fontFamily
                        font.pixelSize: AuraTheme.fontSmall
                        color: AuraTheme.textPrimary
                    }
                    Text {
                        visible: taskRow.modelData.remind_at && taskRow.modelData.remind_at.length > 0
                        text: "напомнить: " + (taskRow.modelData.remind_at || "").replace("T", " ").substring(0, 16)
                        font.family: AuraTheme.fontFamily
                        font.pixelSize: AuraTheme.fontMicro
                        color: AuraTheme.textMuted
                    }
                }

                GlassButton {
                    variant: "quiet"
                    text: ""
                    glyph: "✓"
                    implicitWidth: 40
                    implicitHeight: 34
                    onClicked: App.completeTask(taskRow.modelData.id)
                }
                GlassButton {
                    variant: "quiet"
                    text: ""
                    glyph: "🗑"
                    implicitWidth: 40
                    implicitHeight: 34
                    onClicked: App.deleteTask(taskRow.modelData.id)
                }
            }
        }
    }
}
