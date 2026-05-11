import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import QtCore
import QtQuick.Dialogs
import QtQuick.Effects

Window {
    width: 900
    height: 600
    visible: true
    title: "Aether P2P Messenger"
    color: "#1e1e1e" // Темно-серый фон окна

    // Локальное хранилище настроек (синхронизировано с C++)
    Settings {
        id: appSettings
        property string myName: "Аноним"
    }

    // Текущий открытый чат
    property int activeContactId: -1
    property string activeContactName: ""
    property string activeContactOriginalName: ""

    // Слушаем обновления "настоящего имени" на лету
    Connections {
        target: appCore
        function onOriginalNameUpdated(contactId, originalName) {
            if (contactId === activeContactId) {
                activeContactOriginalName = originalName
            }
        }
    }

    // Универсальный стиль для всех кнопок в приложении
    component StyledButton: Rectangle {
        id: control
        
        property alias text: btnText.text
        property alias font: btnText.font
        property string iconSource: "" // Новое свойство для картинки
        signal clicked()

        // Автоматический размер кнопки под текст
        implicitWidth: iconSource !== "" ? 40 : btnText.implicitWidth + 24
        implicitHeight: iconSource !== "" ? 40 : btnText.implicitHeight + 14
        
        // Фон прозрачный для иконок в покое. При наведении светлеет до белого. Текстовые кнопки остаются серыми.
        color: mouseArea.pressed ? "#d0d0d0" : (mouseArea.containsMouse ? "#ffffff" : (control.iconSource !== "" ? "transparent" : "#333333"))
        radius: 5
        Behavior on color { ColorAnimation { duration: 150 } } // Плавная смена цвета фона

        // Своя картинка-иконка
        Image {
            id: btnIcon
            anchors.centerIn: parent
            source: control.iconSource
            visible: false // Скрываем оригинальную картинку, её будет рисовать MultiEffect ниже
            width: 22  // Оптимальный размер значка (золотая середина)
            height: 22
            fillMode: Image.PreserveAspectFit
            mipmap: true // Сглаживание краев
        }
        
        // Эффект перекрашивания картинки
        MultiEffect {
            source: btnIcon
            anchors.fill: btnIcon
            visible: control.iconSource !== ""
            colorization: 1.0
            // При наведении иконка переходит в темно-серый, в спокойном состоянии — белая
            colorizationColor: mouseArea.pressed || mouseArea.containsMouse ? "#222222" : "white"
            Behavior on colorizationColor { ColorAnimation { duration: 150 } }
        }

        Text {
            id: btnText
            anchors.centerIn: parent
            visible: control.iconSource === "" // Прячем текст, если есть картинка
            // Текст становится темным, когда кнопка светлеет
            color: mouseArea.pressed || mouseArea.containsMouse ? "#111111" : "white"
            Behavior on color { ColorAnimation { duration: 150 } } // Плавная смена цвета текста
        }
        
        MouseArea {
            id: mouseArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: control.clicked()
        }
    }

    // Функция переключения чата (в будущем это будет вызов C++ метода)
    function loadChat(id, name, originalName) {
        activeContactId = id
        activeContactName = name
        activeContactOriginalName = originalName || ""
        // Запрашиваем C++ загрузить историю из БД для этого контакта
        appCore.requestLoadMessages(id)
        
        // Сбрасываем счетчик непрочитанных (отмечаем как прочитанное)
        appCore.requestMarkChatAsRead(id)
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

    // Функция переименования чата
    function renameChat(newName) {
        if (activeContactId !== -1 && newName.trim() !== "") {
            appCore.requestRenameContact(activeContactId, newName.trim())
            activeContactName = newName.trim()
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

                // Заголовок-логотип
                Image {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 90 // Высота логотипа в пикселях
                    source: "icons/logo.png"
                    fillMode: Image.PreserveAspectFit
                    mipmap: true
                    horizontalAlignment: Image.AlignHCenter
                }

                // Сам список контактов
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: appCore.contactsModel
                    clip: true

                    // Ползунок прокрутки списка контактов
                    ScrollBar.vertical: Basic.ScrollBar {
                        id: contactsScroll
                        policy: ScrollBar.AsNeeded
                        width: 10 // Широкая невидимая зона захвата для мышки
                        padding: 0
                        visible: contactsScroll.size < 1.0 // Скрываем, если всё влезает
                        
                        background: Item {} // Убираем виндовый фон
                        
                        contentItem: Rectangle {
                            // Плавно меняем толщину и цвет только при нажатии
                            implicitWidth: contactsScroll.pressed ? 6 : 3
                            radius: implicitWidth / 2
                            color: contactsScroll.pressed ? "#4a90e2" : "#444444"
                            
                            Behavior on implicitWidth { NumberAnimation { duration: 150; easing.type: Easing.OutQuad } }
                            Behavior on color { ColorAnimation { duration: 150 } }
                        }
                    }

                    // Плавные и заметные анимации при перемещении и добавлении контактов
                    add: Transition { 
                        NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: 300 }
                        NumberAnimation { property: "scale"; from: 0.8; to: 1.0; duration: 300; easing.type: Easing.OutBack }
                    }
                    move: Transition { NumberAnimation { properties: "x,y"; duration: 300; easing.type: Easing.OutBack } }
                    displaced: Transition { NumberAnimation { properties: "x,y"; duration: 300; easing.type: Easing.OutBack } }

                    delegate: ItemDelegate {
                        width: parent.width
                        text: model.name
                        hoverEnabled: true

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

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                Text {
                                    text: model.name
                                    color: "lightgray"
                                    font.pixelSize: 14
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                
                                Text {
                                    text: model.lastMessage ? (model.lastMessage.startsWith("FILE:") ? "[Файл]" : model.lastMessage.replace(/\n/g, " ")) : ""
                                    color: "#888888"
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                    maximumLineCount: 1
                                    Layout.fillWidth: true
                                }
                            }
                            
                            // Маркер непрочитанных сообщений
                            Rectangle {
                                visible: model.unreadCount > 0
                                width: Math.max(20, unreadText.implicitWidth + 10)
                                height: 20
                                radius: 10
                                color: "#4a90e2"
                                Layout.alignment: Qt.AlignVCenter
                                
                                Text {
                                    id: unreadText
                                    anchors.centerIn: parent
                                    text: model.unreadCount > 99 ? "99+" : model.unreadCount
                                    color: "white"
                                    font.pixelSize: 11
                                    font.bold: true
                                }
                            }
                        }

                        onClicked: {
                            // Переключаем чат
                            loadChat(model.id, model.name, model.originalName)
                        }
                    }
                }

                // Нижняя панель с кнопками (Настройки и Добавление)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    StyledButton {
                        iconSource: "icons/settings.png"
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                        onClicked: settingsPopup.open()
                    }

                    Item { Layout.fillWidth: true } // Распорка

                    StyledButton {
                        iconSource: "icons/add.png"
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                        onClicked: addContactPopup.open()
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
                    anchors.leftMargin: 15
                    anchors.rightMargin: 15
                    spacing: 10

                    Text {
                        // Показываем имя текущего собеседника
                        text: activeContactName !== "" ? activeContactName : ""
                        color: "white"
                        font.pixelSize: 16
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        verticalAlignment: Text.AlignVCenter
                    }

                    StyledButton {
                        iconSource: "icons/edit.png"
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                        Layout.alignment: Qt.AlignVCenter
                        visible: activeContactName !== ""
                        onClicked: {
                            renameInput.text = activeContactName
                            renameChatPopup.open()
                        }
                    }

                    StyledButton {
                        iconSource: "icons/delete.png"
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                        Layout.alignment: Qt.AlignVCenter
                        visible: activeContactName !== ""
                        onClicked: chatActionsPopup.open()
                    }

                    StyledButton {
                        iconSource: "icons/close.png"
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                        Layout.alignment: Qt.AlignVCenter
                        visible: activeContactName !== ""
                        onClicked: {
                            activeContactId = -1
                            activeContactName = ""
                        }
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
            
            cacheBuffer: 3000 // Держим элементы в памяти вне экрана, чтобы ползунок не сходил с ума

            // Ползунок прокрутки чата
            ScrollBar.vertical: Basic.ScrollBar {
                id: chatScroll
                policy: ScrollBar.AsNeeded
                width: 10 // Широкая невидимая зона захвата
                padding: 0
                visible: chatScroll.size < 1.0 // Скрываем, если всё влезает
                
                background: Item {} // Убираем виндовый фон
                
                contentItem: Rectangle {
                    // Динамическая толщина и цвет только при нажатии
                    implicitWidth: chatScroll.pressed ? 6 : 3
                    radius: implicitWidth / 2
                    color: chatScroll.pressed ? "#4a90e2" : "#444444"
                    
                    Behavior on implicitWidth { NumberAnimation { duration: 150; easing.type: Easing.OutQuad } }
                    Behavior on color { ColorAnimation { duration: 150 } }
                    Behavior on height { NumberAnimation { duration: 150; easing.type: Easing.OutQuad } } // Сглаживаем редкие скачки высоты
                }
            }

            model: appCore.messagesModel
            
            // Буфер рендеринга: позволяет заранее отрисовывать элементы за краем экрана,
            // чтобы анимации появления работали корректно при прокрутке.
            displayMarginBeginning: 150
            displayMarginEnd: 150
            
            // Умная и плавная прокрутка вниз
            onCountChanged: {
                // Если мы находимся в чате, и пришло сообщение, сразу отмечаем его прочитанным
                if (activeContactId !== -1) {
                    appCore.requestMarkChatAsRead(activeContactId)
                }
            
                Qt.callLater(function() {
                    if (chatView.count === 0) return;
                    
                    var targetY = Math.max(-chatView.topMargin, chatView.contentHeight - chatView.height + chatView.bottomMargin)
                    
                    // Если прыжок слишком большой (например, открыли другой чат с историей) — мотаем мгновенно
                    if (chatView.height === 0 || Math.abs(targetY - chatView.contentY) > chatView.height / 2) {
                        // Используем надежный встроенный метод вместо ручной установки координаты
                        chatView.positionViewAtEnd()
                    } else if (targetY > chatView.contentY) {
                        // Если добавилось 1-2 сообщения — плавно прокручиваем, сдвигая старые сообщения вверх
                        smoothScrollAnim.to = targetY
                        smoothScrollAnim.start()
                    }
                })
            }
            
            PropertyAnimation {
                id: smoothScrollAnim
                target: chatView
                property: "contentY"
                duration: 300
                easing.type: Easing.OutQuad
            }

            // Плавное появление самого пузырька сообщения
            add: Transition {
                NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: 250 }
                NumberAnimation { property: "scale"; from: 0.9; to: 1.0; duration: 250; easing.type: Easing.OutBack }
            }
            
            // Зона для Drag-and-Drop (перетаскивания файлов)
            DropArea {
                id: fileDropArea
                anchors.fill: parent
                
                Rectangle {
                    anchors.fill: parent
                    color: "#2b5278"
                    opacity: 0.85
                    visible: fileDropArea.containsDrag
                    z: 100 // Отрисовываем поверх всех сообщений в чате
                    
                    Text {
                        anchors.centerIn: parent
                        text: "Отпустите файлы здесь для отправки\n(до 100 МБ)"
                        color: "white"
                        font.pixelSize: 18
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
                
                onDropped: (drop) => {
                    if (drop.hasUrls) {
                        // Можно перетаскивать сразу несколько файлов!
                        for (let i = 0; i < drop.urls.length; ++i) {
                            appCore.requestSendFile(activeContactId, drop.urls[i])
                        }
                        drop.accept()
                    }
                }
            }

            delegate: Item {
                id: messageDelegate
                width: ListView.view.width
                height: msgRow.height
                
                property bool isFile: model.text.startsWith("FILE:")
                property string filePath: isFile ? model.text.substring(5).replace(/\\/g, "/") : ""
                property bool isImage: isFile && filePath.match(/\.(jpeg|jpg|png|gif|bmp|webp)$/i) !== null

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
                        width: Math.min(Math.max(isImage ? msgImage.implicitWidth + 20 : msgText.implicitWidth + 20, 60), chatView.width * 0.7)
                        height: (isImage ? msgImage.height : msgText.implicitHeight) + 30

                        // Предпросмотр картинки
                        Image {
                            id: msgImage
                            visible: isImage
                            source: isImage ? "file:///" + filePath : ""
                            fillMode: Image.PreserveAspectFit
                            sourceSize.width: 250 // Оптимизация памяти (картинка сожмется для предпросмотра)
                            sourceSize.height: 250
                            
                            // Строго ограничиваем ширину картинки и пересчитываем высоту с сохранением пропорций
                            width: Math.min(implicitWidth, parent.width - 20)
                            height: isImage && implicitWidth > 0 ? width * (implicitHeight / implicitWidth) : 0
                            
                            anchors.top: parent.top
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.topMargin: 8
                            
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: Qt.openUrlExternally("file:///" + filePath)
                            }
                        }

                        Text {
                            id: msgText
                            visible: !isImage // Прячем текст, если это картинка
                            text: isFile ? "[Файл] " + filePath.substring(filePath.lastIndexOf("/") + 1) : model.text
                            color: isFile ? "#66b2ff" : "white"
                            font.underline: isFile
                            font.pixelSize: 14
                            wrapMode: Text.Wrap
                            anchors.top: parent.top
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.topMargin: 8
                            width: parent.width - 20
                            
                            // Делаем файл кликабельным
                            MouseArea {
                                anchors.fill: parent
                                enabled: isFile
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    Qt.openUrlExternally("file:///" + filePath)
                                }
                            }
                        }

                        Text {
                            text: model.time ? model.time : ""
                            color: model.isMine ? "#a0c0e0" : "#888888"
                            font.pixelSize: 10
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: 6
                        }
                        
                        // Прогресс-бар отправки файла
                        Rectangle {
                            width: parent.width - 16
                            height: 3
                            radius: 1.5
                            color: "#1a3652" // Темный фон полоски
                            visible: model.isMine && model.status === 0 && isFile
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 3
                            anchors.horizontalCenter: parent.horizontalCenter
                            clip: true

                            Rectangle {
                                height: parent.height
                                radius: 1.5
                                color: "#66b2ff" // Яркий синий цвет прогресса
                                width: parent.width * model.uploadProgress
                                
                                Behavior on width { NumberAnimation { duration: 150; easing.type: Easing.OutQuad } }
                            }
                        }
                    }

                    // Индикатор Store-and-Forward (только для своих сообщений)
                    Image {
                        visible: model.isMine && model.status === 0
                        source: "icons/clock.png"
                        width: 16 // Слегка уменьшили часики
                        height: 16
                        fillMode: Image.PreserveAspectFit
                        mipmap: true
                        opacity: 0.6
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 2
                    }
                }
                }
            }

            // Нижняя панель: Поле ввода сообщения
            Rectangle {
                Layout.fillWidth: true
                // Панель теперь автоматически растягивается в высоту при многострочном вводе (до 120px)
                Layout.preferredHeight: Math.min(120, Math.max(60, msgInput.implicitHeight + 20))
                color: "#252525"
                visible: activeContactName !== ""

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 10

                    StyledButton {
                        iconSource: "icons/attach.png"
                        Layout.fillHeight: true
                        Layout.preferredWidth: 40
                        onClicked: fileDialog.open()
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: "#333333"
                        radius: 5
                        border.color: msgInput.activeFocus ? "#4a90e2" : "transparent"

                        Flickable {
                            id: inputFlickable
                            anchors.fill: parent
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            
                            // Кастомный ползунок для поля ввода
                            ScrollBar.vertical: Basic.ScrollBar {
                                id: inputScroll
                                policy: ScrollBar.AsNeeded
                                width: 10
                                rightPadding: 3 // Сдвигаем ползунок левее от края рамки
                                topPadding: 5 // Делаем ползунок чуть короче сверху
                                bottomPadding: 5 // И чуть короче снизу
                                visible: inputScroll.size < 1.0 // Скрываем, если всё влезает
                                
                                background: Item {}
                                contentItem: Rectangle {
                                    implicitWidth: inputScroll.pressed ? 6 : 3
                                    radius: implicitWidth / 2
                                    color: inputScroll.pressed ? "#4a90e2" : "#444444"
                                    Behavior on implicitWidth { NumberAnimation { duration: 150; easing.type: Easing.OutQuad } }
                                }
                            }
                            
                            TextArea.flickable: TextArea {
                                id: msgInput
                                width: parent.width // Жестко ограничиваем ширину для правильного переноса
                                placeholderText: "Напишите сообщение..."
                                color: "white"
                                font.pixelSize: 14
                                wrapMode: Text.Wrap
                                topPadding: 10 // Центрируем математически
                                bottomPadding: 10
                                leftPadding: 10
                                rightPadding: 10
                                background: null // Фон теперь у внешнего Rectangle

                                Keys.onPressed: (event) => {
                                    // Перехват вставки из буфера обмена (Ctrl+V)
                                    if (event.key === Qt.Key_V && (event.modifiers & Qt.ControlModifier)) {
                                        if (appCore.requestPasteFromClipboard(activeContactId)) {
                                            event.accepted = true // Буфер обработан в C++ (это файл/картинка)
                                            return
                                        }
                                    }

                                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                        if (event.modifiers & Qt.ControlModifier || event.modifiers & Qt.ShiftModifier) {
                                            msgInput.insert(msgInput.cursorPosition, "\n")
                                            event.accepted = true
                                        } else {
                                            sendBtn.clicked()
                                            event.accepted = true
                                        }
                                    }
                                }
                            }
                        }
                    }

                    StyledButton {
                    id: sendBtn
                        text: "Отправить"
                        Layout.fillHeight: true
                    onClicked: {
                        if (msgInput.text.trim() !== "") {
                            sendMessage(msgInput.text.trim())
                            msgInput.text = ""
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
        height: 340
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        
        onOpened: {
            localIpField.text = appCore.getLocalIpAddress()
        }

        background: Rectangle {
            color: "#252525"
            border.color: "#333333"
            radius: 8
        }

        ColumnLayout {
            anchors.centerIn: parent
            spacing: 15
            Text { text: "Настройки Aether"; color: "white"; font.pixelSize: 18; font.bold: true; Layout.alignment: Qt.AlignHCenter }
            TextField { 
                Layout.fillWidth: true
                placeholderText: "Ваш никнейм"
                color: "white"
                text: appSettings.myName
                onTextChanged: appSettings.myName = text // Сохраняем имя при каждом вводе
                background: Rectangle { color: "#333333"; radius: 4 } 
            }
            
            Text { text: "Ваши IP-адреса (для друзей):"; color: "#888888"; font.pixelSize: 12 }
            
            TextArea {
                id: localIpField
                Layout.fillWidth: true
                Layout.maximumHeight: 100 // Чтобы окно не растянуло, если IP-шников много
                readOnly: true // Запрещаем редактирование
                color: "#aaaaaa"
                font.pixelSize: 12
                clip: true
                selectByMouse: true // Разрешаем выделять и копировать текст!
                background: Rectangle { 
                    color: "#1e1e1e" 
                    radius: 4 
                    border.color: "#333333" 
                }
            }

            StyledButton { 
                text: "Очистить кэш файлов"
                Layout.fillWidth: true
                onClicked: {
                    appCore.requestClearCache()
                    text = "Очищено ✔"
                }
            }
            StyledButton { text: "Сохранить и закрыть"; Layout.alignment: Qt.AlignHCenter; onClicked: settingsPopup.close() }
        }
    }

    // Диалог действий с чатом (открывается по кнопке с корзинкой)
    Popup {
        id: chatActionsPopup
        width: 250
        height: 180
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
            
            Text { 
                text: "Действия с чатом"
                color: "white" 
                font.pixelSize: 16 
                font.bold: true 
                Layout.alignment: Qt.AlignHCenter 
            }
            
            StyledButton { 
                text: "Очистить историю"
                Layout.fillWidth: true
                onClicked: {
                    clearChat()
                    chatActionsPopup.close()
                }
            }
            
            StyledButton { 
                text: "Удалить контакт"
                Layout.fillWidth: true
                onClicked: {
                    deleteContact()
                    chatActionsPopup.close()
                }
            }
        }
    }

    // Диалог переименования чата
    Popup {
        id: renameChatPopup
        width: 250
        height: 180
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
            
            Text { 
                text: "Переименовать чат"
                color: "white" 
                font.pixelSize: 16 
                font.bold: true 
                Layout.alignment: Qt.AlignHCenter 
            }
            
            Text {
                text: "Настоящее имя: " + activeContactOriginalName
                color: "#888888"
                font.pixelSize: 12
                Layout.alignment: Qt.AlignHCenter
                visible: activeContactOriginalName !== ""
            }
            
            TextField {
                id: renameInput
                Layout.fillWidth: true
                color: "white"
                background: Rectangle { color: "#333333"; radius: 4 }
                onAccepted: saveRenameBtn.clicked()
            }
            
            StyledButton { 
                id: saveRenameBtn
                text: "Сохранить"
                Layout.fillWidth: true
                onClicked: {
                    renameChat(renameInput.text)
                    renameChatPopup.close()
                }
            }
        }
    }

    // Системное окно выбора файла
    FileDialog {
        id: fileDialog
        title: "Выберите файл для отправки (до 100 МБ)"
        onAccepted: {
            appCore.requestSendFile(activeContactId, selectedFile)
        }
    }

    // Диалог добавления нового контакта
    Popup {
        id: addContactPopup
        width: 250
        height: 220
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
            
            Text { 
                text: "Добавить контакт"
                color: "white" 
                font.pixelSize: 16 
                font.bold: true 
                Layout.alignment: Qt.AlignHCenter 
            }
            
            TextField {
                id: newNameInput
                Layout.fillWidth: true
                placeholderText: "Имя контакта..."
                color: "white"
                font.pixelSize: 13
                background: Rectangle { color: "#333333"; radius: 4; border.color: newNameInput.activeFocus ? "#4a90e2" : "transparent" }
            }

            TextField {
                id: newIpInput
                Layout.fillWidth: true
                placeholderText: "IP-адрес (10.147.17.5)..."
                color: "white"
                font.pixelSize: 13
                validator: RegularExpressionValidator { regularExpression: /^((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$/ }
                background: Rectangle { color: "#333333"; radius: 4; border.color: newIpInput.activeFocus ? "#4a90e2" : "transparent" }
            }
            
            StyledButton { 
                text: "Добавить"
                Layout.fillWidth: true
                onClicked: {
                    if (newNameInput.text.trim() !== "" && newIpInput.text.trim() !== "") {
                        appCore.requestAddContact(newNameInput.text.trim(), newIpInput.text.trim())
                        newNameInput.text = ""
                        newIpInput.text = ""
                        addContactPopup.close()
                    }
                }
            }
        }
    }
}