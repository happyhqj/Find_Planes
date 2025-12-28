#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <optional>
#include <random>
#include <cstring>
using namespace std;
typedef float fl;

enum GameState {
    MENU,
    ROLE_SELECTION,
    WAITING_FOR_CLIENT,
    CONNECTING_TO_HOST,
    CUSTOM_PLACEMENT,
    WAITING_FOR_OPPONENT,
    PLAYING,
    GAME_OVER
};

enum NetworkRole { ROLE_NONE, ROLE_HOST, ROLE_CLIENT };

const int GRID_SIZE = 12;
const fl CELL_SIZE = 60.0f;
const fl BORDER_SIZE = 2.0f;
const fl UI_HEIGHT = 50.0f;
const unsigned short PORT = 42042;

struct node {
    int x;
    int y;
    node(int x_ = 0, int y_ = 0) : x(x_), y(y_) {}
};

node plane[4][10] = {
    {node(0, 0), node(1, 0), node(1, -1), node(1, -2), node(1, 1), node(1, 2), node(2, 0), node(3, 0), node(3, -1), node(3, 1)},
    {node(0, 0), node(0, 1), node(-1, 1), node(-2, 1), node(1, 1), node(2, 1), node(0, 2), node(0, 3), node(-1, 3), node(1, 3)},
    {node(0, 0), node(-1, 0), node(-1, -1), node(-1, -2), node(-1, 1), node(-1, 2), node(-2, 0), node(-3, 0), node(-3, -1), node(-3, 1)},
    {node(0, 0), node(0, -1), node(-1, -1), node(-2, -1), node(1, -1), node(2, -1), node(0, -2), node(0, -3), node(-1, -3), node(1, -3)}
};

struct PlayerPlane {
    int x;
    int y;
    int rotation;
    bool placed = false;
};

struct PlayerData {
    int head[GRID_SIZE][GRID_SIZE];
    int body[GRID_SIZE][GRID_SIZE];
    int vis[GRID_SIZE][GRID_SIZE];
    vector<PlayerPlane> planes{4};
    int planesShotDown = 0;
    void reset() {
        memset(head, 0, sizeof(head));
        memset(body, 0, sizeof(body));
        memset(vis, 0, sizeof(vis));
        for (auto &p : planes) p.placed = false;
        planesShotDown = 0;
    }
};

GameState gameState = MENU;
NetworkRole networkRole = ROLE_NONE;
bool isMultiplayerMode = false;

PlayerData player;
PlayerData opponent;

int drawn[GRID_SIZE][GRID_SIZE];

int currentRotation = 0;
bool myTurn = false;
sf::Clock turnSwitchClock;
bool isSwitchingTurn = false;
bool finalTurnTaken = false;

sf::TcpListener listener;
sf::TcpSocket socket;

string hostIpAddress = "127.0.0.1";

int movesLeft = 40;
int planesFound = 0;

bool ipInputActive = false;

mt19937 rng(random_device{}());
unsigned short currentPort = PORT;

bool waitingForAttackReply = false;
int lastAttackX = -1;
int lastAttackY = -1;

bool in_grid(int i, int j) {
    return i >= 0 && i < GRID_SIZE && j >= 0 && j < GRID_SIZE;
}

string getLocalIpAddress() {
    auto localAddress = sf::IpAddress::getLocalAddress();
    if (localAddress && localAddress->toString() != "127.0.0.1") return localAddress->toString();
    auto publicAddress = sf::IpAddress::getPublicAddress(sf::seconds(2));
    if (publicAddress) return publicAddress->toString();
    return "Not Found";
}

void put_plane_into_grid(int rot, int x, int y, int h[GRID_SIZE][GRID_SIZE], int b[GRID_SIZE][GRID_SIZE]) {
    for (int i = 0; i < 10; ++i) {
        int tx = x + plane[rot][i].x;
        int ty = y + plane[rot][i].y;
        if (in_grid(tx, ty)) {
            if (i == 0) h[tx][ty] = 1;
            else b[tx][ty] = 1;
        }
    }
}

bool no_ocp(int i, int j) {
    return !player.head[i][j] && !player.body[i][j];
}

bool ok_to_put(int id, int x, int y) {
    for (int i = 0; i < 10; ++i) {
        int tx = x + plane[id][i].x;
        int ty = y + plane[id][i].y;
        if (!in_grid(tx, ty) || !no_ocp(tx, ty)) return false;
    }
    return true;
}

bool is_placement_valid(int planeIdx, int rot, int x, int y) {
    for (int i = 0; i < 10; ++i) {
        int tx = x + plane[rot][i].x;
        int ty = y + plane[rot][i].y;
        if (!in_grid(tx, ty)) return false;
        for (int p_idx = 0; p_idx < 4; ++p_idx) {
            if (p_idx == planeIdx || !player.planes[p_idx].placed) continue;
            for (int j = 0; j < 10; ++j) {
                int other_tx = player.planes[p_idx].x + plane[player.planes[p_idx].rotation][j].x;
                int other_ty = player.planes[p_idx].y + plane[player.planes[p_idx].rotation][j].y;
                if (tx == other_tx && ty == other_ty) return false;
            }
        }
    }
    return true;
}

void find_pos() {
    memset(player.head, 0, sizeof(player.head));
    memset(player.body, 0, sizeof(player.body));
    for (int plane_count = 0; plane_count < 4; ++plane_count) {
        vector<node> available_cells;
        for (int i = 0; i < GRID_SIZE; ++i)
            for (int j = 0; j < GRID_SIZE; ++j)
                available_cells.emplace_back(i, j);
        vector<int> plane_id = {0, 1, 2, 3};
        shuffle(available_cells.begin(), available_cells.end(), rng);
        shuffle(plane_id.begin(), plane_id.end(), rng);
        bool placed = false;
        for (const auto &cell : available_cells) {
            for (int id : plane_id) {
                if (ok_to_put(id, cell.x, cell.y)) {
                    put_plane_into_grid(id, cell.x, cell.y, player.head, player.body);
                    placed = true;
                    break;
                }
            }
            if (placed) break;
        }
    }
}

void resetGame() {
    player.reset();
    opponent.reset();
    memset(drawn, 0, sizeof(drawn));
    currentRotation = 0;
    movesLeft = 40;
    planesFound = 0;
    isSwitchingTurn = false;
    finalTurnTaken = false;
    waitingForAttackReply = false;
    if (isMultiplayerMode) myTurn = (networkRole == ROLE_HOST);
    else find_pos();
}

void returnToMenu(sf::Text &statusText) {
    socket.disconnect();
    listener.close();
    networkRole = ROLE_NONE;
    isMultiplayerMode = false;
    ipInputActive = false;
    waitingForAttackReply = false;
    statusText.setString("");
    gameState = MENU;
    cout << "Returned to Menu. Network resources released." << endl;
}

void drawGrid(sf::RenderWindow &window, const PlayerData &dataToDraw, bool showPlanes, bool isOpponentGrid) {
    for (int i = 0; i < GRID_SIZE; ++i) {
        for (int j = 0; j < GRID_SIZE; ++j) {
            sf::RectangleShape cell({CELL_SIZE - BORDER_SIZE, CELL_SIZE - BORDER_SIZE});
            cell.setPosition({i * CELL_SIZE + BORDER_SIZE / 2.f, j * CELL_SIZE + BORDER_SIZE / 2.f + UI_HEIGHT});
            int current_vis = isOpponentGrid ? opponent.vis[i][j] : player.vis[i][j]; // 这格到底有没有被“看过”（我打过 / 对手打过）
            cell.setFillColor(current_vis ? sf::Color(200, 200, 200) : sf::Color(100, 100, 100)); // 基础底色：看过 = 浅灰，没看过 = 深灰
            if (isOpponentGrid && opponent.vis[i][j]) { // 打中对手机身
                if (opponent.body[i][j]) cell.setFillColor(sf::Color::Green);
            } else if (!isOpponentGrid && player.vis[i][j] && player.body[i][j]) { // 对手打中自己机身
                cell.setFillColor(sf::Color::Green);
            }
            if (showPlanes) { // 游戏结束，全部显示出来
                if (dataToDraw.head[i][j]) cell.setFillColor(sf::Color::Red);
                else if (dataToDraw.body[i][j]) cell.setFillColor(sf::Color(0, 200, 0));
            }
            window.draw(cell);
            if ((isOpponentGrid || !isMultiplayerMode) && drawn[i][j]) { // 在对手图上做标记
                sf::RectangleShape d({CELL_SIZE - BORDER_SIZE, CELL_SIZE - BORDER_SIZE});
                d.setPosition({i * CELL_SIZE + BORDER_SIZE / 2.f, j * CELL_SIZE + BORDER_SIZE / 2.f + UI_HEIGHT});
                d.setFillColor(sf::Color(0, 100, 255, 128));
                window.draw(d);
            }
            if (current_vis && dataToDraw.head[i][j]) { // 机头被打掉了
                sf::RectangleShape headRect({CELL_SIZE - BORDER_SIZE, CELL_SIZE - BORDER_SIZE});
                headRect.setPosition({i * CELL_SIZE + BORDER_SIZE / 2.f, j * CELL_SIZE + BORDER_SIZE / 2.f + UI_HEIGHT});
                headRect.setFillColor(sf::Color::Red);
                window.draw(headRect);
            }
            if (!isOpponentGrid && isMultiplayerMode && player.vis[i][j]) { //在自己图中被打掉的位置上显示‘X’
                float cx = i * CELL_SIZE + CELL_SIZE / 2.f;
                float cy = j * CELL_SIZE + UI_HEIGHT + CELL_SIZE / 2.f;
                float len = CELL_SIZE * 0.65f;
                sf::RectangleShape l1({len, 3.f});
                l1.setFillColor(sf::Color::Black);
                l1.setOrigin({len / 2.f, 1.5f});
                l1.setPosition({cx, cy});
                l1.setRotation(sf::degrees(45.f));
                window.draw(l1);
                sf::RectangleShape l2({len, 3.f});
                l2.setFillColor(sf::Color::Black);
                l2.setOrigin({len / 2.f, 1.5f});
                l2.setPosition({cx, cy});
                l2.setRotation(sf::degrees(-45.f));
                window.draw(l2);
            }
        }
    }
}

int main() {
    sf::ContextSettings settings;
    settings.antiAliasingLevel = 8;
    sf::RenderWindow window(
        sf::VideoMode(
            {static_cast<unsigned int>(GRID_SIZE * CELL_SIZE),
             static_cast<unsigned int>(GRID_SIZE * CELL_SIZE + UI_HEIGHT)}),
        "Find Planes - LAN Edition", sf::Style::Default, sf::State::Windowed,
        settings);
    window.setFramerateLimit(60);

    sf::Font font;
    if (!font.openFromFile("arial.ttf"))
        if (!font.openFromFile("/System/Library/Fonts/Supplemental/Arial.ttf"))
            return -1;

    sf::Text titleText(font, "Find Planes", 50);
    sf::Text singlePlayerButton(font, "Single Player", 40);
    sf::Text multiplayerButton(font, "Multiplayer (LAN)", 40);
    sf::Text exitButton(font, "Exit Game", 40);

    sf::Text hostButton(font, "Host Game", 40);
    sf::Text joinButton(font, "Join Game", 40);

    sf::Text statusText(font, "", 24);
    sf::Text ipInputText(font, hostIpAddress, 24);
    sf::Text ipInputLabel(font, "Enter Host LAN IP:", 24);

    sf::Text turnText(font, "", 30);
    sf::Text gameOverText(font, "", 60);
    sf::Text mainMenuButton(font, "Main Menu", 40);

    sf::Text placementText(font, "", 20);
    sf::Text counterText(font, "", 24);
    sf::Text headsLeftText(font, "", 24);
    sf::Text backToMenuButton(font, "Back to Menu", 24);

    sf::RectangleShape ipInputBox({320.f, 30.f});

    sf::Text mpPlanesText(font, "", 24);
    mpPlanesText.setFillColor(sf::Color::White);
    mpPlanesText.setPosition({15.f, 10.f});

    titleText.setFillColor(sf::Color::White);
    {
        sf::FloatRect r = titleText.getLocalBounds();
        titleText.setOrigin({r.position.x + r.size.x / 2.f, r.position.y + r.size.y / 2.f});
        titleText.setPosition({GRID_SIZE * CELL_SIZE / 2.f, 150.f});
    }
    singlePlayerButton.setFillColor(sf::Color::White);
    {
        sf::FloatRect r = singlePlayerButton.getLocalBounds();
        singlePlayerButton.setOrigin({r.position.x + r.size.x / 2.f, r.position.y + r.size.y / 2.f});
        singlePlayerButton.setPosition({GRID_SIZE * CELL_SIZE / 2.f, 300.f});
    }
    multiplayerButton.setFillColor(sf::Color::White);
    {
        sf::FloatRect r = multiplayerButton.getLocalBounds();
        multiplayerButton.setOrigin({r.position.x + r.size.x / 2.f, r.position.y + r.size.y / 2.f});
        multiplayerButton.setPosition({GRID_SIZE * CELL_SIZE / 2.f, 400.f});
    }
    exitButton.setFillColor(sf::Color::White);
    {
        sf::FloatRect r = exitButton.getLocalBounds();
        exitButton.setOrigin({r.position.x + r.size.x / 2.f, r.position.y + r.size.y / 2.f});
        exitButton.setPosition({GRID_SIZE * CELL_SIZE / 2.f, 500.f});
    }

    hostButton.setFillColor(sf::Color::White);
    {
        sf::FloatRect r = hostButton.getLocalBounds();
        hostButton.setOrigin({r.position.x + r.size.x / 2.f, r.position.y + r.size.y / 2.f});
        hostButton.setPosition({GRID_SIZE * CELL_SIZE / 2.f, 300.f});
    }
    joinButton.setFillColor(sf::Color::White);
    {
        sf::FloatRect r = joinButton.getLocalBounds();
        joinButton.setOrigin({r.position.x + r.size.x / 2.f, r.position.y + r.size.y / 2.f});
        joinButton.setPosition({GRID_SIZE * CELL_SIZE / 2.f, 400.f});
    }

    statusText.setFillColor(sf::Color::Yellow);
    ipInputLabel.setFillColor(sf::Color::White);
    ipInputLabel.setPosition({GRID_SIZE * CELL_SIZE / 2.f - 160.f, 450.f});

    ipInputText.setFillColor(sf::Color::White);
    ipInputText.setPosition({GRID_SIZE * CELL_SIZE / 2.f - 155.f, 480.f});

    ipInputBox.setPosition({GRID_SIZE * CELL_SIZE / 2.f - 160.f, 480.f});
    ipInputBox.setFillColor(sf::Color::Transparent);
    ipInputBox.setOutlineThickness(2.f);
    ipInputBox.setOutlineColor(sf::Color(100, 100, 100));

    turnText.setFillColor(sf::Color::White);

    mainMenuButton.setFillColor(sf::Color::Green);

    placementText.setPosition({15.f, 12.f});
    counterText.setPosition({15.f, 10.f});

    backToMenuButton.setFillColor(sf::Color::White);
    {
        sf::FloatRect r = backToMenuButton.getLocalBounds();
        backToMenuButton.setOrigin({r.position.x + r.size.x / 2.f, r.position.y + r.size.y / 2.f});
        backToMenuButton.setPosition({GRID_SIZE * CELL_SIZE / 2.f, UI_HEIGHT / 2.f});
    }

    while (window.isOpen()) { // 核心主程序

        if (isMultiplayerMode) { // 一、多人模式底层逻辑
            if (gameState == WAITING_FOR_CLIENT) { // 等待客户端接入
                sf::Socket::Status accStatus = listener.accept(socket);
                if (accStatus == sf::Socket::Status::Done) {
                    cout << "Client connected: " << socket.getRemoteAddress()->toString() << endl;
                    socket.setBlocking(false);
                    resetGame();
                    gameState = CUSTOM_PLACEMENT;
                }
            }
            if (gameState == WAITING_FOR_OPPONENT) { // 等待对手摆完飞机
                sf::Packet packet;
                sf::Socket::Status rs = socket.receive(packet);
                if (rs == sf::Socket::Status::Done) {
                    string msg;
                    if ((packet >> msg) && msg == "ready") {
                        cout << "Opponent is ready. Starting game." << endl;
                        for (const auto &pl : player.planes) { // 将数据放入数组中
                            if (pl.placed)
                                put_plane_into_grid(pl.rotation, pl.x, pl.y, player.head, player.body);
                        }
                        gameState = PLAYING;
                    }
                } else if (rs == sf::Socket::Status::Disconnected) {
                    returnToMenu(statusText);
                }
            }
            if (gameState == PLAYING) {
                if (myTurn && waitingForAttackReply) { // 我打出去了，等对面回复
                    sf::Packet replyPacket;
                    sf::Socket::Status rs = socket.receive(replyPacket);
                    if (rs == sf::Socket::Status::Done) {
                        bool hit = false;
                        bool body = false;
                        if (replyPacket >> hit >> body) { // 对手告诉我打中了什么东西
                            cout << "Received attack reply: hit = " << hit << ", body = " << body << endl;
                            waitingForAttackReply = false; // 等待结束
                            if (hit) { // 打中头
                                opponent.head[lastAttackX][lastAttackY] = 1;
                                opponent.planesShotDown++;
                            } if (body) opponent.body[lastAttackX][lastAttackY] = 1; // 打中身子
                            if (opponent.planesShotDown == 4) { // 如果全部击落
                                if (networkRole == ROLE_HOST && !finalTurnTaken) { // 给对手最后一次机会
                                    finalTurnTaken = true;
                                    isSwitchingTurn = true;
                                    turnSwitchClock.restart();
                                } else { // 否则直接赢
                                    gameState = GAME_OVER;
                                    gameOverText.setString(finalTurnTaken ? "Draw!" : "You Win!");
                                }
                            } else if (finalTurnTaken) { // 最后一轮，但没打完，输
                                gameState = GAME_OVER;
                                gameOverText.setString("You Lose!");
                            } else { // 否则进入下一轮次
                                isSwitchingTurn = true;
                                turnSwitchClock.restart();
                            }
                        }
                    } else if (rs == sf::Socket::Status::Disconnected) {
                        returnToMenu(statusText);
                    }
                }
                if (!myTurn && !isSwitchingTurn) { // 对面打我
                    sf::Packet attackPacket;
                    sf::Socket::Status rs = socket.receive(attackPacket);
                    if (rs == sf::Socket::Status::Done) {
                        int x = -1, y = -1;
                        if (attackPacket >> x >> y) { // 解包对方的攻击坐标
                            cout << "Received attack at (" << x << ", " << y << ")" << endl;
                            player.vis[x][y] = 1;
                            bool isHead = (player.head[x][y] == 1);
                            bool isBody = (player.body[x][y] == 1);
                            if (isHead) player.planesShotDown++;
                            sf::Packet replyPacket;
                            replyPacket << isHead << isBody; // 返回打中了什么
                            (void)socket.send(replyPacket);
                            if (player.planesShotDown == 4) { // 我被全打掉了
                                if (networkRole == ROLE_CLIENT && !finalTurnTaken) { // 我是后手，还有一次机会
                                    finalTurnTaken = true;
                                    isSwitchingTurn = true;
                                    turnSwitchClock.restart();
                                } else if (networkRole == ROLE_HOST && finalTurnTaken) { // 我是先手，并且刚才是后手的最后一击，平局
                                    gameState = GAME_OVER;
                                    gameOverText.setString("Draw!");
                                } else { // 否则我输了
                                    gameState = GAME_OVER;
                                    gameOverText.setString("You Lose!");
                                }
                            } else {
                                if (networkRole == ROLE_HOST && finalTurnTaken) { // 我是先手，对手没把握住最后一击，我赢
                                    gameState = GAME_OVER;
                                    gameOverText.setString("You Win!");
                                } else { // 否则继续交换
                                    isSwitchingTurn = true;
                                    turnSwitchClock.restart();
                                }
                            }
                        }
                    } else if (rs == sf::Socket::Status::Disconnected) {
                        returnToMenu(statusText);
                    }
                }
                if (isSwitchingTurn && turnSwitchClock.getElapsedTime().asSeconds() > 2.f) { // 等待两秒
                    isSwitchingTurn = false;
                    myTurn = !myTurn;
                }
            }
        }

        while (auto event = window.pollEvent()) { // 二、处理事件
            if (event->is<sf::Event::Closed>()) { // 关闭窗口
                returnToMenu(statusText);
                resetGame();
                hostIpAddress = "127.0.0.1";
                ipInputText.setString(hostIpAddress);
                window.close();
            }
            if (gameState != MENU && gameState != GAME_OVER) { // 对所有不再 menu 和 game_over 的界面，判断是否点击 back to menu
                if (auto mb = event->getIf<sf::Event::MouseButtonPressed>()) {
                    if (mb->button == sf::Mouse::Button::Left) {
                        sf::Vector2f mousePos(static_cast<float>(mb->position.x), static_cast<float>(mb->position.y));
                        if (backToMenuButton.getGlobalBounds().contains(mousePos)) {
                            returnToMenu(statusText);
                            continue; // 避免下面 switch 再处理一遍
                        }
                    }
                }
            }
            switch (gameState) { // 最核心的交互部分！！！
                case MENU: {
                    if (auto mm = event->getIf<sf::Event::MouseMoved>()) { // 鼠标移上去变黄
                        sf::Vector2f m(static_cast<float>(mm->position.x), static_cast<float>(mm->position.y));
                        singlePlayerButton.setFillColor(singlePlayerButton.getGlobalBounds().contains(m) ? sf::Color::Yellow : sf::Color::White);
                        multiplayerButton.setFillColor(multiplayerButton.getGlobalBounds().contains(m) ? sf::Color::Yellow : sf::Color::White);
                        exitButton.setFillColor(exitButton.getGlobalBounds().contains(m) ? sf::Color::Yellow : sf::Color::White);
                    }
                    if (auto mp = event->getIf<sf::Event::MouseButtonPressed>()) { // 处理菜单点击
                        if (mp->button == sf::Mouse::Button::Left) {
                            sf::Vector2f m(static_cast<float>(mp->position.x), static_cast<float>(mp->position.y));
                            if (singlePlayerButton.getGlobalBounds().contains(m)) { // 点单机
                                isMultiplayerMode = false;
                                resetGame();
                                gameState = PLAYING;
                            }
                            if (multiplayerButton.getGlobalBounds().contains(m)) { // 点联机
                                isMultiplayerMode = true;
                                gameState = ROLE_SELECTION;
                            }
                            if (exitButton.getGlobalBounds().contains(m)) { // 点退出游戏
                                window.close();
                            }
                        }
                    }
                    break;
                }
                case ROLE_SELECTION: // 界面相似，三合一
                case CONNECTING_TO_HOST:
                case WAITING_FOR_CLIENT: {
                    if (auto mm = event->getIf<sf::Event::MouseMoved>()) { // 给 host 和 join 上色
                        sf::Vector2f m(static_cast<float>(mm->position.x), static_cast<float>(mm->position.y));
                        hostButton.setFillColor(hostButton.getGlobalBounds().contains(m) ? sf::Color::Yellow : sf::Color::White);
                        joinButton.setFillColor(joinButton.getGlobalBounds().contains(m) ? sf::Color::Yellow : sf::Color::White);
                    }
                    if (auto mp = event->getIf<sf::Event::MouseButtonPressed>()) { // 处理点击事件
                        if (mp->button == sf::Mouse::Button::Left) {
                            sf::Vector2f m(static_cast<float>(mp->position.x), static_cast<float>(mp->position.y));
                            if (hostButton.getGlobalBounds().contains(m)) { // 点击 host
                                networkRole = ROLE_HOST;
                                currentPort = static_cast<unsigned short>(40000 + (rng() % 20001)); // 随机端口
                                listener.close();
                                sf::Socket::Status ls = listener.listen(currentPort);
                                if (ls != sf::Socket::Status::Done) {
                                    statusText.setString("Port " + to_string(currentPort) + " is busy!");
                                } else {
                                    listener.setBlocking(false);
                                    statusText.setString("Your LAN IP: " + getLocalIpAddress() + ":" + to_string(currentPort) + "\n\n" "Waiting for a player to join...");
                                    gameState = WAITING_FOR_CLIENT;
                                }
                            }
                            if (joinButton.getGlobalBounds().contains(m)) { // 点击 join
                                ipInputActive = true; // 打开输入框
                                networkRole = ROLE_CLIENT;
                                gameState = CONNECTING_TO_HOST;
                            }
                            if (ipInputActive && !ipInputBox.getGlobalBounds().contains(m) && !joinButton.getGlobalBounds().contains(m)) { // 点到外面就取消读入
                                ipInputActive = false;
                            }
                        }
                    }
                    if (ipInputActive) { // 处理 IP 读入
                        if (auto te = event->getIf<sf::Event::TextEntered>()) {
                            if (te->unicode == '\b') {
                                if (!hostIpAddress.empty()) hostIpAddress.pop_back();
                            } else if (te->unicode < 128 && te->unicode != '\r' && te->unicode != '\n') {
                                hostIpAddress += static_cast<char>(te->unicode);
                            }
                            ipInputText.setString(hostIpAddress);
                        }
                    }
                    if (auto kp = event->getIf<sf::Event::KeyPressed>()) { // 处理 hostIpAddress 并连接
                        if (kp->code == sf::Keyboard::Key::Enter && ipInputActive) {
                            string ipStr = hostIpAddress;
                            unsigned short portToConnect = PORT;
                            size_t pos = ipStr.find(':');
                            if (pos != string::npos) {
                                string portStr = ipStr.substr(pos + 1);
                                ipStr = ipStr.substr(0, pos);
                                int pv = 0;
                                try {
                                    pv = stoi(portStr);
                                } catch (...) {
                                    pv = PORT;
                                }
                                if (pv > 0 && pv < 65536) portToConnect = static_cast<unsigned short>(pv);
                            }
                            statusText.setString("Connecting to " + ipStr + ":" + to_string(portToConnect) + "...");
                            std::optional<sf::IpAddress> addr = sf::IpAddress::resolve(ipStr);
                            if (!addr) {
                                statusText.setString("Invalid IP address format!");
                            } else {
                                sf::Socket::Status cs = socket.connect(*addr, portToConnect, sf::seconds(5)); // 尝试连接 IP
                                if (cs != sf::Socket::Status::Done) {
                                    statusText.setString("Connection failed! Check IP or firewall.");
                                } else {
                                    cout << "Connected to host." << endl;
                                    socket.setBlocking(false);
                                    resetGame();
                                    ipInputActive = false;
                                    gameState = CUSTOM_PLACEMENT;
                                }
                            }
                        }
                    }
                    break;
                }
                case CUSTOM_PLACEMENT: {
                    if (auto mp = event->getIf<sf::Event::MouseButtonPressed>()) {
                        if (mp->position.y > UI_HEIGHT) {
                            int gx = mp->position.x / CELL_SIZE;
                            int gy = (mp->position.y - UI_HEIGHT) / CELL_SIZE;
                            if (in_grid(gx, gy)) { // 点击的位置在 grid 里
                                if (mp->button == sf::Mouse::Button::Right) { // 右键旋转飞机方向
                                    currentRotation = (currentRotation + 1) % 4;
                                }
                                if (mp->button == sf::Mouse::Button::Left) { // 左键
                                    int nextPlane = -1;
                                    for (int i = 0; i < 4; ++i) {
                                        if (!player.planes[i].placed) {
                                            nextPlane = i;
                                            break;
                                        }
                                    }
                                    if (nextPlane != -1 && is_placement_valid(nextPlane, currentRotation, gx, gy)) { // 如果可以放置，就直接放
                                        player.planes[nextPlane].placed = true;
                                        player.planes[nextPlane].x = gx;
                                        player.planes[nextPlane].y = gy;
                                        player.planes[nextPlane].rotation = currentRotation;
                                        int placedCount = 0;
                                        for (const auto &p : player.planes) if (p.placed) placedCount++;
                                        if (placedCount == 4) {
                                            sf::Packet p;
                                            p << string("ready");
                                            (void)socket.send(p); // 向对手返回 ‘ready’
                                            cout << "My placement is ready, sent notification." << endl;
                                            gameState = WAITING_FOR_OPPONENT; // 放完了，进入等待阶段
                                        }
                                    }
                                }
                            }
                        }
                    }
                    break;
                }
                case WAITING_FOR_OPPONENT: { // 等待对手
                    break;
                }
                case PLAYING: {
                    if (auto mp = event->getIf<sf::Event::MouseButtonPressed>()) {
                        if (mp->position.y > UI_HEIGHT) {
                            int gx = mp->position.x / CELL_SIZE;
                            int gy = (mp->position.y - UI_HEIGHT) / CELL_SIZE;
                            if (in_grid(gx, gy)) {
                                if (!isMultiplayerMode) { // 单机
                                    if (mp->button == sf::Mouse::Button::Left && player.vis[gx][gy] == 0) {
                                        player.vis[gx][gy] = 1;
                                        movesLeft--;
                                        if (player.head[gx][gy] == 1) planesFound++;
                                    } else if (mp->button == sf::Mouse::Button::Right) {
                                        drawn[gx][gy] = 1 - drawn[gx][gy];
                                    }
                                } else { // 联机
                                    if (myTurn && !isSwitchingTurn && !waitingForAttackReply) {
                                        if (mp->button == sf::Mouse::Button::Left && opponent.vis[gx][gy] == 0) {
                                            opponent.vis[gx][gy] = 1;
                                            lastAttackX = gx;
                                            lastAttackY = gy;
                                            sf::Packet attackPacket;
                                            attackPacket << gx << gy;
                                            (void)socket.send(attackPacket); // 向对手发送进攻坐标
                                            waitingForAttackReply = true;
                                            cout << "Sent attack at (" << gx << ", " << gy << "), waiting for reply..." << endl;
                                        } else if (mp->button == sf::Mouse::Button::Right) {
                                            drawn[gx][gy] = 1 - drawn[gx][gy];
                                        }
                                    }
                                }
                            }
                        }
                    }
                    if (!isMultiplayerMode && (planesFound == 4 || movesLeft <= 0)) { // 单机游戏结束，跳转 game_over
                        gameState = GAME_OVER;
                        if (planesFound == 4) gameOverText.setString("You Win!");
                        else gameOverText.setString("You Lose!");
                        sf::FloatRect goB = gameOverText.getLocalBounds();
                        gameOverText.setOrigin({goB.position.x + goB.size.x / 2.f, goB.position.y + goB.size.y / 2.f});
                        gameOverText.setPosition({GRID_SIZE * CELL_SIZE / 2.f, 300.f});
                        sf::FloatRect rstB = mainMenuButton.getLocalBounds();
                        mainMenuButton.setOrigin({rstB.position.x + rstB.size.x / 2.f, rstB.position.y + rstB.size.y / 2.f});
                        mainMenuButton.setPosition({GRID_SIZE * CELL_SIZE / 2.f, 400.f});
                    }
                    break;
                }
                case GAME_OVER: {
                    if (auto mm = event->getIf<sf::Event::MouseMoved>()) {
                        sf::Vector2f m(static_cast<float>(mm->position.x), static_cast<float>(mm->position.y));
                        mainMenuButton.setFillColor(mainMenuButton.getGlobalBounds().contains(m) ? sf::Color::Yellow : sf::Color::Green);
                    }
                    if (auto mp = event->getIf<sf::Event::MouseButtonPressed>()) {
                        if (mp->button == sf::Mouse::Button::Left) {
                            sf::Vector2f m(static_cast<float>(mp->position.x), static_cast<float>(mp->position.y));
                            if (mainMenuButton.getGlobalBounds().contains(m)) {
                                returnToMenu(statusText);
                            }
                        }
                    }
                    break;
                }
                default:
                    break;
            }
        }

        window.clear(sf::Color(50, 50, 50));
        if (gameState != MENU && gameState != GAME_OVER) { // 显示 back_to_menu 按键
            sf::Vector2f mp(sf::Mouse::getPosition(window));
            bool hover = backToMenuButton.getGlobalBounds().contains(mp);
            backToMenuButton.setFillColor(hover ? sf::Color::Yellow : sf::Color::White);
        }

        switch (gameState) { // 三、渲染部分
            case MENU: { // 渲染菜单
                window.draw(titleText);
                window.draw(singlePlayerButton);
                window.draw(multiplayerButton);
                window.draw(exitButton);
                break;
            }
            case ROLE_SELECTION: // 界面类似，三合一
            case CONNECTING_TO_HOST:
            case WAITING_FOR_CLIENT: {
                sf::RectangleShape ui({static_cast<fl>(GRID_SIZE * CELL_SIZE), UI_HEIGHT});
                ui.setFillColor(sf::Color(30, 30, 30));
                window.draw(ui);
                window.draw(backToMenuButton);
                if (gameState != WAITING_FOR_CLIENT) { // 渲染 WAITING_FOR_CLIENT
                    window.draw(hostButton);
                    window.draw(joinButton);
                }
                if (gameState == CONNECTING_TO_HOST || gameState == ROLE_SELECTION) {  // 渲染 CONNECTING_TO_HOST 和 ROLE_SELECTION
                    window.draw(ipInputLabel);
                    ipInputBox.setOutlineColor(ipInputActive ? sf::Color::Yellow : sf::Color(100, 100, 100));
                    window.draw(ipInputBox);
                    window.draw(ipInputText);
                }
                { // 渲染状态文字
                    sf::FloatRect ssb = statusText.getLocalBounds();
                    statusText.setOrigin({ssb.position.x + ssb.size.x / 2.f, ssb.position.y + ssb.size.y / 2.f});
                    statusText.setPosition({GRID_SIZE * CELL_SIZE / 2.f, (gameState == WAITING_FOR_CLIENT) ? GRID_SIZE * CELL_SIZE / 2.f : 550.f});
                    window.draw(statusText);
                }
                break;
            }
            case CUSTOM_PLACEMENT:
            case WAITING_FOR_OPPONENT: { // 渲染放置飞机界面
                sf::RectangleShape ui({static_cast<fl>(GRID_SIZE * CELL_SIZE), UI_HEIGHT});
                ui.setFillColor(sf::Color(30, 30, 30));
                window.draw(ui);
                if (!isMultiplayerMode) {
                    window.draw(backToMenuButton);
                }
                int placedCount = 0;
                for (const auto &p : player.planes)
                    if (p.placed) placedCount++;
                if (gameState == CUSTOM_PLACEMENT) {
                    placementText.setString("Place plane " + to_string(placedCount) + "/4 (RMB to rotate)");
                } else {
                    placementText.setString("Place plane 4/4 (waiting...)");
                }
                window.draw(placementText);
                for (int i = 0; i < GRID_SIZE; ++i) { // 画网格底板
                    for (int j = 0; j < GRID_SIZE; ++j) {
                        sf::RectangleShape c({CELL_SIZE - BORDER_SIZE, CELL_SIZE - BORDER_SIZE});
                        c.setPosition({i * CELL_SIZE + BORDER_SIZE / 2.f, j * CELL_SIZE + BORDER_SIZE / 2.f + UI_HEIGHT});
                        c.setFillColor(sf::Color(100, 100, 100));
                        window.draw(c);
                    }
                }
                for (const auto &p : player.planes) { // 画已放好的飞机
                    if (p.placed) {
                        for (int i = 0; i < 10; ++i) {
                            int tx = p.x + plane[p.rotation][i].x;
                            int ty = p.y + plane[p.rotation][i].y;
                            if (in_grid(tx, ty)) {
                                sf::RectangleShape part({CELL_SIZE - BORDER_SIZE, CELL_SIZE - BORDER_SIZE});
                                part.setPosition({static_cast<fl>(tx) * CELL_SIZE + BORDER_SIZE / 2.f, static_cast<fl>(ty) * CELL_SIZE + BORDER_SIZE / 2.f + UI_HEIGHT});
                                part.setFillColor(i == 0 ? sf::Color::Red : sf::Color(0, 200, 0));
                                window.draw(part);
                            }
                        }
                    }
                }
                if (gameState == WAITING_FOR_OPPONENT) { // 等待阶段
                    sf::FloatRect ssb = statusText.getLocalBounds();
                    statusText.setOrigin({ssb.position.x + ssb.size.x / 2.f, ssb.position.y + ssb.size.y / 2.f});
                    statusText.setString("Waiting for opponent...");
                    statusText.setPosition({GRID_SIZE * CELL_SIZE / 2.f, GRID_SIZE * CELL_SIZE / 2.f});
                    window.draw(statusText);
                } else { // 根据我鼠标的位置添加飞机
                    sf::Vector2i mp = sf::Mouse::getPosition(window);
                    if (mp.y > UI_HEIGHT) {
                        int gx = mp.x / CELL_SIZE;
                        int gy = (mp.y - UI_HEIGHT) / CELL_SIZE;
                        int nextPlane = -1;
                        for (int i = 0; i < 4; ++i) {
                            if (!player.planes[i].placed) {
                                nextPlane = i;
                                break;
                            }
                        }
                        if (nextPlane != -1 && in_grid(gx, gy)) {
                            bool can = is_placement_valid(nextPlane, currentRotation, gx, gy);
                            for (int i = 0; i < 10; ++i) {
                                int tx = gx + plane[currentRotation][i].x;
                                int ty = gy + plane[currentRotation][i].y;
                                if (in_grid(tx, ty)) {
                                    sf::RectangleShape part({CELL_SIZE - BORDER_SIZE, CELL_SIZE - BORDER_SIZE});
                                    part.setPosition({static_cast<fl>(tx) * CELL_SIZE + BORDER_SIZE / 2.f, static_cast<fl>(ty) * CELL_SIZE + BORDER_SIZE / 2.f + UI_HEIGHT});
                                    if (can) part.setFillColor(i == 0 ? sf::Color(255, 100, 100, 180) : sf::Color(100, 255, 100, 180)); // 如果这个飞机能全放下，就显示正常颜色
                                    else part.setFillColor(sf::Color(255, 0, 0, 128)); // 否则显示半透明红色表示不可放置
                                    window.draw(part);
                                }
                            }
                        }
                    }
                }
                break;
            }
            case PLAYING:
            case GAME_OVER: {
                sf::RectangleShape ui({static_cast<fl>(GRID_SIZE * CELL_SIZE), UI_HEIGHT});
                ui.setFillColor(sf::Color(30, 30, 30));
                window.draw(ui);
                if (gameState == PLAYING && !isMultiplayerMode) { // 单机显示 back_to_menu 按键
                    window.draw(backToMenuButton);
                }
                if (!isMultiplayerMode) { // 单机更多显示
                    drawGrid(window, player, (gameState == GAME_OVER), false);
                    counterText.setString("Moves Left: " + to_string(movesLeft));
                    window.draw(counterText);
                    headsLeftText.setString("Heads Left: " + to_string(4 - planesFound));
                    sf::FloatRect hlB = headsLeftText.getLocalBounds();
                    headsLeftText.setOrigin({hlB.position.x + hlB.size.x, hlB.position.y});
                    headsLeftText.setPosition({GRID_SIZE * CELL_SIZE - 15.f, 10.f});
                    window.draw(headsLeftText);
                } else { // 联机显示
                    int myLeft = 4 - player.planesShotDown;
                    int oppLeft = 4 - opponent.planesShotDown;
                    mpPlanesText.setString("Me: " + to_string(myLeft) + "   Opp: " + to_string(oppLeft));
                    window.draw(mpPlanesText); // 绘制双方剩余多少飞机
                    if (isSwitchingTurn) {
                        if (myTurn) { 
                            drawGrid(window, opponent, (gameState == GAME_OVER), true);
                        } else {
                            drawGrid(window, player, true, false);
                        }
                        turnText.setString("Switching Turn..."); 
                    } else if (myTurn) { // 显示对手地图
                        drawGrid(window, opponent, (gameState == GAME_OVER), true);
                        turnText.setString(waitingForAttackReply ? "Waiting for Reply..." : "Your Turn!");
                    } else { // 完整地显示自己地图
                        drawGrid(window, player, true, false);
                        turnText.setString("Opponent's Turn");
                    }
                    sf::FloatRect ttb = turnText.getLocalBounds(); // 在中间显示 turntext
                    turnText.setOrigin({ttb.position.x + ttb.size.x / 2.f, ttb.position.y + ttb.size.y / 2.f});
                    turnText.setPosition({GRID_SIZE * CELL_SIZE / 2.f, UI_HEIGHT / 2.f});
                    window.draw(turnText);
                }
                if (gameState == GAME_OVER) { // 渲染结束画面
                    if (isMultiplayerMode) {
                        if (myTurn) drawGrid(window, opponent, true, true);
                        else drawGrid(window, player, true, false);
                    }
                    sf::RectangleShape overlay(sf::Vector2f(window.getSize()));
                    overlay.setFillColor(sf::Color(0, 0, 0, 150));
                    window.draw(overlay);
                    sf::FloatRect goB = gameOverText.getLocalBounds();
                    gameOverText.setOrigin({goB.position.x + goB.size.x / 2.f, goB.position.y + goB.size.y / 2.f});
                    gameOverText.setPosition({GRID_SIZE * CELL_SIZE / 2.f, 300.f});
                    sf::FloatRect rstB = mainMenuButton.getLocalBounds();
                    mainMenuButton.setOrigin({rstB.position.x + rstB.size.x / 2.f, rstB.position.y + rstB.size.y / 2.f});
                    mainMenuButton.setPosition({GRID_SIZE * CELL_SIZE / 2.f, 400.f});
                    window.draw(gameOverText);
                    window.draw(mainMenuButton);
                }
                break;
            }
            default:
                break;
        }
        window.display();
    }

    return 0;
}
