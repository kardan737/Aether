import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Window {
    width: 900
    height: 600
    visible: true
    title: "Aether P2P Messenger"
    color: "#1e1e1e" // Темно-серый фон окна

    // Текущий открытый чат
    property int activeContactId: -1
    property string activeContactName: ""

    // Функция переключения чата (в будущем это будет вызов C++ метода)
    function loadChat(id, name) {
        activeContactId = id
        activeContactName = name
        // Запрашиваем C++ загрузить историю из БД для этого контакта
        appCore.requestLoadMessages(id)
    }

    // Функция отправки сообщения
    function sendMessage(text) {
        if (activeContactId !== -1) {
            // Отправляем сообщение в C++, где оно запишется в БД
            appCore.requestSendMessage(activeContactId, text)
        }
    }

    // Функция очистки истории
    function clearChat() {
        if (activeContactId !== -1) {
            appCore.requestClearChat(activeContactId)
        }
    }

    // Функция удаления контакта
    function deleteContact() {
        if (activeContactId !== -1) {
            appCore.requestDeleteContact(activeContactId)
            activeContactId = -1
            activeContactName = ""
        }
    }

    // Горизонтальная сетка: делит окно на левую и правую части
    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ==========================================
        // ЛЕВАЯ ПАНЕЛЬ: Список контактов
        // ==========================================
        Rectangle {
            Layout.preferredWidth: 250
            Layout.fillHeight: true
            color: "#252525" // Чуть светлее фона

            // Вертикальная стопка элементов левой панели
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 10

                // Заголовок и кнопка настроек
                RowLayout {
                    Layout.fillWidth: true
                    
                    Text {
                        text: "Контактная сеть"
                        color: "white"
                        font.pixelSize: 18
                        font.bold: true
                        Layout.fillWidth: true
                    }
                    
                    Button {
                        text: "⚙"
                        Layout.preferredWidth: 35
                        onClicked: settingsPopup.open()
                    }
                }

                // Поля ввода нового контакта (Имя и IP)
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5

                    TextField {
                        id: newNameInput
                        Layout.fillWidth: true
                        placeholderText: "Имя контакта..."
                        color: "white"
                        font.pixelSize: 13
                        background: Rectangle {
                            color: "#333333"
                            radius: 4
                            border.color: newNameInput.activeFocus ? "#4a90e2" : "transparent"
                        }
                    }

                    TextField {
                        id: newIpInput
                        Layout.fillWidth: true
                        placeholderText: "IP-адрес (напр. 10.147.17.5)..."
                        color: "white"
                        font.pixelSize: 13
                        background: Rectangle {
                            color: "#333333"
                            radius: 4
                            border.color: newIpInput.activeFocus ? "#4a90e2" : "transparent"
                        }
                    }

                    Button {
                        text: "Добавить контакт"
                        Layout.fillWidth: true
                        onClicked: {
                            if (newNameInput.text.trim() !== "" && newIpInput.text.trim() !== "") {
                                appCore.requestAddContact(newNameInput.text.trim(), newIpInput.text.trim())
                                newNameInput.text = ""
                                newIpInput.text = ""
                            }
                        }
                    }
                }

                // Сам список контактов
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: appCore.contactsModel
                    clip: true

                    delegate: ItemDelegate {
                        width: parent.width
                        text: model.name
                        hoverEnabled: true
                    
                    // Визуализация Data Decay (Эффект старения)
                    opacity: 1.0 - (model.decayLevel * 0.7)

                        // Умный фон: меняет цвет при наведении и нажатии
                        background: Rectangle {
                            color: parent.pressed ? "#555555" : (parent.hovered ? "#333333" : "transparent")
                            radius: 5
                        }

                        contentItem: RowLayout {
                            spacing: 10
                            
                            // Индикатор сети (зеленый - онлайн, красный - оффлайн)
                            Rectangle {
                                width: 10
                                height: 10
                                radius: 5
                                color: model.isOnline ? "#4caf50" : "#f44336"
                                Layout.leftMargin: 5
                            }

                            Text {
                                text: model.name
                                color: "lightgray"
                                font.pixelSize: 14
                                verticalAlignment: Text.AlignVCenter
                                Layout.fillWidth: true
                            }
                        }

                        onClicked: {
                            // Переключаем чат
                            loadChat(model.id, model.name)
                        }
                    }
                }
            }

            // Тонкая разделительная линия справа
            Rectangle {
                width: 1
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                color: "#333333"
            }
        }

        // ==========================================
        // ПРАВАЯ ПАНЕЛЬ: Чат
        // ==========================================
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Верхняя плашка чата
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 50
                color: "#252525"
                
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 10

                    Text {
                        // Показываем имя текущего собеседника
                        text: activeContactName !== "" ? "Чат: " + activeContactName : "Выберите контакт"
                        color: "white"
                        font.pixelSize: 16
                        Layout.fillWidth: true
                    }

                    Button {
                        text: "Закрыть чат"
                        visible: activeContactName !== ""
                        onClicked: {
                            activeContactId = -1
                            activeContactName = ""
                        }
                    }

                    Button {
                        text: "Очистить чат"
                        visible: activeContactName !== ""
                        onClicked: clearChat()
                    }

                    Button {
                        text: "Удалить контакт"
                        visible: activeContactName !== ""
                        onClicked: deleteContact()
                    }
                }
            }

            // Заглушка, когда чат не выбран
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: activeContactName === ""
                
                Text {
                    anchors.centerIn: parent
                    text: "Выберите чат для начала общения"
                    color: "#555555"
                    font.pixelSize: 16
                }
            }

        // Область сообщений (чат)
        ListView {
            id: chatView
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: activeContactName !== ""
            clip: true
            spacing: 15
            topMargin: 15
            bottomMargin: 15

            model: appCore.messagesModel

            delegate: Item {
                width: ListView.view.width
                height: msgRow.height

                Row {
                    id: msgRow
                    anchors.right: model.isMine ? parent.right : undefined
                    anchors.left: !model.isMine ? parent.left : undefined
                    anchors.rightMargin: 20
                    anchors.leftMargin: 20
                    spacing: 8
                    layoutDirection: model.isMine ? Qt.RightToLeft : Qt.LeftToRight

                    Rectangle {
                        color: model.isMine ? "#2b5278" : "#333333" // Синий для себя, серый для собеседника
                        radius: 8
                        width: Math.min(Math.max(msgText.implicitWidth + 20, 60), chatView.width * 0.7)
                        height: msgText.implicitHeight + 30

                        Text {
                            id: msgText
                            text: model.text
                            color: "white"
                            font.pixelSize: 14
                            wrapMode: Text.Wrap
                            anchors.top: parent.top
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.topMargin: 8
                            width: parent.width - 20
                        }

                        Text {
                            text: model.time ? model.time : ""
                            color: model.isMine ? "#a0c0e0" : "#888888"
                            font.pixelSize: 10
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: 6
                        }
                    }

                    // Индикатор Store-and-Forward (только для своих сообщений)
                    Text {
                        visible: model.isMine
                        text: model.status === 0 ? "🕒" : "✓"
                        color: model.status === 0 ? "#999999" : "#4a90e2"
                        font.pixelSize: 14
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 2
                    }
                }
                }
            }

            // Нижняя панель: Поле ввода сообщения
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 60
                color: "#252525"
                visible: activeContactName !== ""

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 10

                    Button {
                        text: "📎"
                        Layout.fillHeight: true
                        Layout.preferredWidth: 40
                        onClicked: console.log("Aether: Ожидание C++ бэкенда для выбора файла (лимит 100 МБ)")
                    }

                    TextField {
                    id: msgInput
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        placeholderText: "Напишите сообщение..."
                        color: "white"
                        font.pixelSize: 14

                        background: Rectangle {
                            color: "#333333"
                            radius: 5
                            border.color: parent.activeFocus ? "#4a90e2" : "transparent"
                        }

                    onAccepted: sendBtn.clicked()
                    }

                    Button {
                    id: sendBtn
                        text: "Отправить"
                        Layout.fillHeight: true
                    onClicked: {
                        if (msgInput.text.trim() !== "") {
                            sendMessage(msgInput.text.trim())
                            msgInput.text = ""
                            chatView.positionViewAtEnd()
                        }
                    }
                    }
                }
            }
        }
    }

    // Диалог настроек
    Popup {
        id: settingsPopup
        width: 300
        height: 250
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: "#252525"
            border.color: "#333333"
            radius: 8
        }

        ColumnLayout {
            anchors.centerIn: parent
            spacing: 15
            Text { text: "Настройки Aether"; color: "white"; font.pixelSize: 18; font.bold: true; Layout.alignment: Qt.AlignHCenter }
            TextField { placeholderText: "P2P Порт (по-умолч. 8080)"; color: "white"; background: Rectangle { color: "#333333"; radius: 4 } }
            TextField { placeholderText: "Ваш никнейм"; color: "white"; background: Rectangle { color: "#333333"; radius: 4 } }
            Button { text: "Сохранить и закрыть"; Layout.alignment: Qt.AlignHCenter; onClicked: settingsPopup.close() }
        }
    }
}