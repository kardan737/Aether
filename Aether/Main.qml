import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Window {
    width: 900
    height: 600
    visible: true
    title: "Aether P2P Messenger"
    color: "#1e1e1e" // Темно-серый фон окна

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

            // Динамическая модель контактов
            ListModel {
                id: contactsModel
                ListElement { name: "Вадим (Б01-401)" }
                ListElement { name: "Влад (Б01-401)" }
                ListElement { name: "Данила (Me)" }
            }

            // Вертикальная стопка элементов левой панели
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 10

                // Заголовок
                Text {
                    text: "Контактная сеть"
                    color: "white"
                    font.pixelSize: 18
                    font.bold: true
                }

                // Поле ввода нового контакта
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 5

                    TextField {
                        id: newContactInput
                        Layout.fillWidth: true
                        placeholderText: "Ник или IP..."
                        color: "white"
                        font.pixelSize: 13

                        background: Rectangle {
                            color: "#333333"
                            radius: 4
                            border.color: newContactInput.activeFocus ? "#4a90e2" : "transparent"
                        }

                        // Добавление по нажатию Enter
                        onAccepted: {
                            if (text.trim() !== "") {
                                contactsModel.append({"name": text})
                                text = "" // Очищаем поле
                            }
                        }
                    }

                    Button {
                        text: "+"
                        Layout.preferredWidth: 40
                        onClicked: {
                            if (newContactInput.text.trim() !== "") {
                                contactsModel.append({"name": newContactInput.text})
                                newContactInput.text = "" // Очищаем поле
                            }
                        }
                    }
                }

                // Сам список контактов
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: contactsModel
                    clip: true

                    delegate: ItemDelegate {
                        width: parent.width
                        text: model.name
                        hoverEnabled: true

                        // Умный фон: меняет цвет при наведении и нажатии
                        background: Rectangle {
                            color: parent.pressed ? "#555555" : (parent.hovered ? "#333333" : "transparent")
                            radius: 5
                        }

                        contentItem: Text {
                            text: model.name
                            color: "lightgray"
                            font.pixelSize: 14
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: 10
                        }

                        onClicked: {
                            console.log("Открываем чат с: " + model.name)
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
                Text {
                    anchors.centerIn: parent
                    text: "Общий канал (P2P)"
                    color: "white"
                    font.pixelSize: 16
                }
            }

            // Область сообщений (пока пустая)
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "transparent"
                Text {
                    anchors.centerIn: parent
                    text: "Здесь будут ваши сообщения..."
                    color: "#555555"
                    font.pixelSize: 14
                }
            }

            // Нижняя панель: Поле ввода сообщения
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 60
                color: "#252525"

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 10

                    TextField {
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
                    }

                    Button {
                        text: "Отправить"
                        Layout.fillHeight: true
                        onClicked: console.log("Нажали отправить сообщение!")
                    }
                }
            }
        }
    }
}