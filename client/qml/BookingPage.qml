import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Selfvoy
import "Format.js" as F

/*
  BookingPage — «Запись»: страницы записи владельца (booking.*). Постоянная
  ссылка #/b/<slug>: люди без Selfvoy сами выбирают свободное время (страница гостя —
  в веб-клиенте). Здесь — создать/изменить страницу, пауза, удаление, ссылка
  (копировать) и записи гостей с отменой. Обновляется по уведомлениям booking.*.
*/
Item {
    id: page

    signal navigate(string to)

    property var pages: App ? App.bookingPages : []
    property var bookings: App ? App.guestBookings : []
    property bool formOpen: false
    property var editing: null          // страница в правке (null — новая)
    property string formError: ""

    function link(p) { return p.url && p.url.length > 0 ? p.url : "#/b/" + p.slug }

    function openForm(p) {
        editing = p
        formError = ""
        titleField.text = p ? p.title : ""
        descriptionField.text = p ? p.description : ""
        form.duration = p ? p.duration_minutes : 30
        form.days = p ? p.days_ahead : 14
        perDayField.text = String(p ? p.max_per_day : 4)
        noticeField.text = String(p ? p.notice_hours : 2)
        seriesField.text = String(p ? p.max_series : 1)
        weekendsSwitch.checked = p ? p.weekends === true : false
        formOpen = true
    }

    function upcoming(b) {
        var now = new Date()
        return (b.occurrences || []).filter(function (o) { return o.status === "active" && F.parse(o.start) > now })
    }

    // Встречи по времени начала: после переноса сервер оставляет встречу на прежнем месте массива.
    function byStart(list) {
        return list.slice().sort(function (a, b) {
            var x = F.parse(a.start), y = F.parse(b.start)
            return (x ? x.getTime() : 0) - (y ? y.getTime() : 0)
        })
    }

    Connections {
        target: App
        function onBookingPageSaved() { page.formOpen = false; page.editing = null }
        function onBookingPageFailed(message) { page.formError = message }
    }

    Component.onCompleted: if (App) App.loadBooking()

    component Caption: Text {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        font.family: SelfvoyTheme.fontFamily
        font.pixelSize: SelfvoyTheme.fontMicro
        color: SelfvoyTheme.textMuted
    }

    component GlassSwitch: Switch {
        id: sw
        padding: 0
        implicitWidth: 46
        implicitHeight: 26
        indicator: Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: sw.checked ? SelfvoyTheme.accentA(0.85) : Qt.rgba(1, 1, 1, 0.12)
            border.width: 1
            border.color: sw.checked ? SelfvoyTheme.lilacA(0.5) : Qt.rgba(1, 1, 1, 0.18)
            Rectangle {
                width: 20; height: 20; radius: 10
                y: 3
                x: sw.checked ? parent.width - width - 3 : 3
                color: "#FFFFFF"
            }
        }
        contentItem: null
    }

    Flickable {
        anchors.fill: parent
        contentHeight: column.implicitHeight + 88
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: column
            x: 48
            y: 44
            width: parent.width - 96
            spacing: 18

            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: "Запись"
                    font.family: SelfvoyTheme.fontFamily
                    font.pixelSize: 38
                    font.weight: Font.Bold
                    color: SelfvoyTheme.textPrimary
                }
                GlassButton {
                    visible: !page.formOpen
                    text: "Новая страница"
                    symbol: "plus"
                    variant: "accent"
                    onClicked: page.openForm(null)
                }
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "Постоянная ссылка: люди без Selfvoy сами выбирают свободное время в вашем расписании — можно сразу серией раз в неделю."
                font.family: SelfvoyTheme.fontFamily
                font.pixelSize: SelfvoyTheme.fontBody
                color: SelfvoyTheme.textSecondary
            }

            // ------------------------------------------------ форма страницы
            GlassPanel {
                id: form
                visible: page.formOpen
                Layout.fillWidth: true
                radius: 24
                padding: 24
                backdrop: null
                property int duration: 30
                property int days: 14

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 12

                    Text {
                        text: page.editing ? "Изменить страницу" : "Новая страница записи"
                        font.family: SelfvoyTheme.fontFamily
                        font.pixelSize: SelfvoyTheme.fontTitle
                        font.weight: Font.DemiBold
                        color: SelfvoyTheme.textPrimary
                    }
                    GlassField {
                        id: titleField
                        Layout.fillWidth: true
                        label: "Название"
                        placeholderText: "Например, консультация"
                        onTextEdited: page.formError = ""
                    }
                    GlassField {
                        id: descriptionField
                        Layout.fillWidth: true
                        label: "Описание для гостей"
                        placeholderText: "Что будет на встрече, где и как (необязательно)"
                    }
                    Caption { text: "Длительность" }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        Repeater {
                            model: [{ v: 15, t: "15 мин" }, { v: 30, t: "30 мин" }, { v: 45, t: "45 мин" }, { v: 60, t: "1 час" }, { v: 90, t: "1,5 часа" }]
                            delegate: Chip {
                                required property var modelData
                                text: modelData.t
                                active: form.duration === modelData.v
                                onClicked: form.duration = modelData.v
                            }
                        }
                    }
                    Caption { text: "Запись открыта на" }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        Repeater {
                            model: [{ v: 7, t: "Неделя" }, { v: 14, t: "2 недели" }, { v: 30, t: "Месяц" }, { v: 60, t: "2 месяца" }]
                            delegate: Chip {
                                required property var modelData
                                text: modelData.t
                                active: form.days === modelData.v
                                onClicked: form.days = modelData.v
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        GlassField {
                            id: perDayField
                            Layout.fillWidth: true
                            label: "Встреч в день, не больше"
                            validator: IntValidator { bottom: 0; top: 99 }
                        }
                        GlassField {
                            id: noticeField
                            Layout.fillWidth: true
                            label: "Запас до встречи, часов"
                            validator: IntValidator { bottom: 0; top: 999 }
                        }
                        GlassField {
                            id: seriesField
                            Layout.fillWidth: true
                            label: "Серия, встреч раз в неделю"
                            validator: IntValidator { bottom: 0; top: 99 }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: "Выходные"
                                font.family: SelfvoyTheme.fontFamily
                                font.pixelSize: SelfvoyTheme.fontBody
                                font.weight: Font.DemiBold
                                color: SelfvoyTheme.textPrimary
                            }
                            Caption { text: weekendsSwitch.checked ? "Будни и выходные" : "Только будни" }
                        }
                        GlassSwitch { id: weekendsSwitch }
                    }
                    Text {
                        visible: page.formError.length > 0
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: page.formError
                        font.family: SelfvoyTheme.fontFamily
                        font.pixelSize: SelfvoyTheme.fontSmall
                        color: SelfvoyTheme.danger
                    }
                    RowLayout {
                        spacing: 10
                        GlassButton {
                            // Пока запрос в полёте — неактивна: двойной клик не создаёт дубль страницы.
                            readonly property bool saving: App ? App.bookingSaving : false
                            text: saving ? "Сохраняем…" : (page.editing ? "Сохранить" : "Создать страницу")
                            variant: "accent"
                            busy: saving
                            enabled: !saving
                            onClicked: App.saveBookingPage(page.editing ? page.editing.id : 0, {
                                title: titleField.text,
                                description: descriptionField.text,
                                duration_minutes: form.duration,
                                days_ahead: form.days,
                                max_per_day: parseInt(perDayField.text || "0"),
                                notice_hours: noticeField.text.length > 0 ? parseInt(noticeField.text) : -1,
                                max_series: parseInt(seriesField.text || "0"),
                                weekends: weekendsSwitch.checked
                            })
                        }
                        GlassButton {
                            text: "Отмена"
                            variant: "quiet"
                            onClicked: { page.formOpen = false; page.editing = null }
                        }
                    }
                }
            }

            // ------------------------------------------------ страницы
            Text {
                text: "Страницы"
                font.family: SelfvoyTheme.fontFamily
                font.pixelSize: SelfvoyTheme.fontTitle
                font.weight: Font.DemiBold
                color: SelfvoyTheme.textPrimary
            }
            Caption {
                visible: App && App.bookingLoaded && page.pages.length === 0
                text: "Создайте первую страницу — например, «Консультация, 30 минут» — и отправьте ссылку клиентам."
            }
            Repeater {
                model: page.pages
                delegate: GlassPanel {
                    id: card
                    required property var modelData
                    property bool confirmDelete: false
                    Layout.fillWidth: true
                    radius: 22
                    padding: 20
                    backdrop: null

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: card.modelData.title
                                elide: Text.ElideRight
                                font.family: SelfvoyTheme.fontFamily
                                font.pixelSize: SelfvoyTheme.fontBody
                                font.weight: Font.DemiBold
                                color: SelfvoyTheme.textPrimary
                            }
                            Text {
                                text: card.modelData.status === "active" ? "Открыта" : "На паузе"
                                font.family: SelfvoyTheme.fontFamily
                                font.pixelSize: SelfvoyTheme.fontMicro
                                color: card.modelData.status === "active" ? SelfvoyTheme.textSecondary : SelfvoyTheme.warning
                            }
                        }
                        Caption {
                            text: [card.modelData.duration_minutes + " мин",
                                   "на " + card.modelData.days_ahead + " " + F.plural(card.modelData.days_ahead, "день", "дня", "дней") + " вперёд",
                                   "до " + card.modelData.max_per_day + " в день",
                                   card.modelData.max_series > 1 ? "серия до " + card.modelData.max_series : "",
                                   card.modelData.weekends ? "и в выходные" : "только будни"].filter(function (s) { return s.length > 0 }).join(" · ")
                        }
                        Caption { visible: card.modelData.description.length > 0; text: card.modelData.description }
                        TextEdit {
                            Layout.fillWidth: true
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.WrapAnywhere
                            text: page.link(card.modelData)
                            font.family: SelfvoyTheme.fontFamily
                            font.pixelSize: SelfvoyTheme.fontSmall
                            color: SelfvoyTheme.textPrimary
                        }
                        Caption {
                            visible: !card.modelData.url
                            text: "Сервер не знает свой публичный адрес (SELFVOY_PUBLIC_URL) — полную ссылку соберите из адреса веб-клиента."
                        }
                        Caption {
                            text: card.modelData.upcoming > 0
                                  ? "Предстоит: " + card.modelData.upcoming + " " + F.plural(card.modelData.upcoming, "встреча", "встречи", "встреч")
                                  : "Пока никто не записался"
                        }
                        RowLayout {
                            spacing: 8
                            GlassButton { text: "Копировать ссылку"; onClicked: App.copyText(page.link(card.modelData)) }
                            GlassButton { text: "Изменить"; variant: "quiet"; onClicked: page.openForm(card.modelData) }
                            GlassButton {
                                text: card.modelData.status === "active" ? "Пауза" : "Открыть"
                                variant: "quiet"
                                onClicked: App.setBookingPagePaused(card.modelData.id, card.modelData.status === "active")
                            }
                            GlassButton {
                                visible: !card.confirmDelete
                                text: "Удалить"
                                variant: "quiet"
                                onClicked: card.confirmDelete = true
                            }
                            GlassButton {
                                visible: card.confirmDelete
                                text: "Да, удалить"
                                variant: "danger"
                                onClicked: { card.confirmDelete = false; App.deleteBookingPage(card.modelData.id) }
                            }
                            GlassButton {
                                visible: card.confirmDelete
                                text: "Нет"
                                variant: "quiet"
                                onClicked: card.confirmDelete = false
                            }
                        }
                    }
                }
            }

            // ------------------------------------------------ записи гостей
            Text {
                text: "Записи гостей"
                font.family: SelfvoyTheme.fontFamily
                font.pixelSize: SelfvoyTheme.fontTitle
                font.weight: Font.DemiBold
                color: SelfvoyTheme.textPrimary
            }
            Caption {
                visible: page.bookings.length === 0
                text: "Здесь появятся люди, которые записались по вашим ссылкам."
            }
            Repeater {
                model: page.bookings
                delegate: GlassPanel {
                    id: row
                    required property var modelData
                    readonly property var next: page.upcoming(modelData)
                    readonly property string state_: modelData.status === "cancelled"
                                                     ? (modelData.cancelled_by === "owner" ? "Отменили вы" : "Отменил гость")
                                                     : (next.length > 0 ? "Предстоит" : "Прошла")
                    property bool confirming: false
                    Layout.fillWidth: true
                    radius: 20
                    padding: 18
                    backdrop: null

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: row.modelData.guest_name + " · " + row.modelData.page_title
                                elide: Text.ElideRight
                                font.family: SelfvoyTheme.fontFamily
                                font.pixelSize: SelfvoyTheme.fontBody
                                font.weight: Font.DemiBold
                                color: SelfvoyTheme.textPrimary
                            }
                            Text {
                                text: row.state_
                                font.family: SelfvoyTheme.fontFamily
                                font.pixelSize: SelfvoyTheme.fontMicro
                                color: row.modelData.status === "cancelled" ? SelfvoyTheme.danger : SelfvoyTheme.textSecondary
                            }
                        }
                        Caption {
                            text: page.byStart(row.next.length > 0 ? row.next : (row.modelData.occurrences || [])).slice(0, 3)
                                  .map(function (o) { return F.dayPhrase(o.start) + ", " + F.time(o.start) }).join(" · ")
                                  + (row.modelData.reschedules > 0 ? " · переносов: " + row.modelData.reschedules : "")
                        }
                        Caption { visible: row.modelData.guest_email.length > 0; text: row.modelData.guest_email }
                        Caption { visible: row.modelData.guest_comment.length > 0; text: "«" + row.modelData.guest_comment + "»" }
                        RowLayout {
                            visible: row.state_ === "Предстоит"
                            spacing: 8
                            GlassButton {
                                visible: !row.confirming
                                text: "Отменить запись"
                                variant: "quiet"
                                onClicked: row.confirming = true
                            }
                            GlassButton {
                                visible: row.confirming
                                text: "Да, отменить все встречи"
                                variant: "danger"
                                onClicked: { row.confirming = false; App.cancelGuestBooking(row.modelData.id) }
                            }
                            GlassButton {
                                visible: row.confirming
                                text: "Нет"
                                variant: "quiet"
                                onClicked: row.confirming = false
                            }
                        }
                    }
                }
            }
        }
    }
}
