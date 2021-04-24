#include <iostream>
#include <cmath>
#include <chrono>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <cassert>

using namespace std;
using namespace chrono;

static constexpr int MAX_STEPS = 300;

static constexpr int FIELD_WIDTH = 12;
static constexpr int FIELD_HEIGHT = 9;

/******************************************** solution constants ******************************************************/

static constexpr int SCORE_FOR_CAPTURED_HOUSE = 1000;
static constexpr int SCORE_FOR_LOST_HOUSE = -150;


static constexpr int SCORE_FOR_UNINHABITED_FRIEND_CLOWN = -100;
static constexpr int SCORE_FOR_BLOCKED_FRIEND_CLOWN = -100;

static constexpr int SCORE_FOR_UNINHABITED_ENEMY_CLOWN = 1000;
static constexpr int SCORE_FOR_BLOCKED_ENEMY_CLOWN = 10;


static constexpr int SCORE_FOR_UNINHABITED_FRIEND_STRONGMAN = -50;
static constexpr int SCORE_FOR_BLOCKED_FRIEND_STRONGMAN = -150;

static constexpr int SCORE_FOR_UNINHABITED_ENEMY_STRONGMAN = 100;
static constexpr int SCORE_FOR_BLOCKED_ENEMY_STRONGMAN = 25;


static constexpr int SCORE_FOR_UNINHABITED_FRIEND_ACROBAT = -20;
static constexpr int SCORE_FOR_BLOCKED_FRIEND_ACROBAT = -300;

static constexpr int SCORE_FOR_UNINHABITED_ENEMY_ACROBAT = 50;
static constexpr int SCORE_FOR_BLOCKED_ENEMY_ACROBAT = 20;


static constexpr int SCORE_FOR_UNINHABITED_FRIEND_MAGICIAN = -20;
static constexpr int SCORE_FOR_BLOCKED_FRIEND_MAGICIAN = -500;

static constexpr int SCORE_FOR_UNINHABITED_ENEMY_MAGICIAN = -10;
static constexpr int SCORE_FOR_BLOCKED_ENEMY_MAGICIAN = 40;


static constexpr int SCORE_FOR_UNINHABITED_FRIEND_TRAINER = -SCORE_FOR_CAPTURED_HOUSE;

static constexpr int SCORE_FOR_UNINHABITED_ENEMY_TRAINER = -10;


static constexpr int SCORE_DISTANCE_TO_END_MULTIPLIER = 1;
static constexpr int SCORE_DISTANCE_TO_HOUSE_MULTIPLIER = 2;

/******************************************** game structures *********************************************************/

struct Cell {
    int row = -1, col = 25;

    bool operator==(const Cell &right) const {
        return row == right.row && col == right.col;
    }

    bool isInFieldBounds() const {
        return row >= 0 && row < FIELD_HEIGHT && col >= 0 && col < FIELD_WIDTH;
    }
};

template<>
struct std::hash<Cell> {
    size_t operator()(const Cell &cell) const {
        return ((size_t) cell.row << 32) + cell.col;
    }
};

struct Move {
    Cell from, to;

    bool operator==(const Move &right) const {
        return from == right.from && to == right.to;
    }
};

const Cell NONE_CELL{};
const Move NONE_MOVE{NONE_CELL, NONE_CELL};

struct Entity {
    enum EntityType {
        CLOWN = 0,      // 0b000
        STRONGMAN = 2,  // 0b010
        ACROBAT = 4,    // 0b100
        MAGICIAN = 5,   // 0b101
        TRAINER = 6,    // 0b110
        NONE_TYPE = -1,
    };

    /* const */ int id;
    /* const */ int ownerId;
    /* const */ EntityType type;

    static int idOf(const int ownerId, const EntityType type, bool isSecond = false) {
        return (ownerId << 3) + (int) type + (int) isSecond;
    }

    static EntityType typeById(const int id) {
        switch (id & 0b111) {
            case 0:
            case 1:
                return CLOWN;

            case 2:
            case 3:
                return STRONGMAN;

            case 5:
                return MAGICIAN;

            case 6:
                return TRAINER;

            default:
                return NONE_TYPE;
        }
    }

    Entity(const int ownerId, const EntityType type, bool isSecond = false) :
            id(idOf(ownerId, type, isSecond)),
            ownerId(ownerId),
            type(type) {}

    explicit Entity(const int id) :
            id(id),
            ownerId(id >> 3),
            type(typeById(id)) {}
};


const Entity NONE_ENTITY(-1, Entity::NONE_TYPE); // NOLINT(cert-err58-cpp)

struct CellInfo {
    /*const*/ bool hasHouse = false;
    Entity entity = NONE_ENTITY;
};

struct Field {
    /*const*/ unordered_set<Cell> houses;

    CellInfo field[FIELD_WIDTH][FIELD_HEIGHT];
    unordered_map<int, Cell> positions;

    unordered_set<Cell> freeHouses;
    unordered_set<int> activeEntities;

    CellInfo &operator[](const Cell cell) {
        return field[cell.col][cell.row];
    }

    const CellInfo &operator[](const Cell cell) const {
        return field[cell.col][cell.row];
    }

    void set(const int row, const int col, const Entity entity) {
        set(Cell{row, col}, entity);
    }

    void set(const Cell cell, const Entity entity) {
        (*this)[cell].entity = entity;
        positions[entity.id] = cell;
    }

    void clear(const Cell cell) {
        (*this)[cell].entity = NONE_ENTITY;
    }

    enum MoveType {
        ILLEGAL_MOVE,
        NO_MOVE,
        BASE_MOVE,
        DOUBLE_MOVE,
        SWAP,
        PUSH,
    };

    MoveType checkMove(const Move move) const {
        // NONE_MOVE is always legal
        if (move == NONE_MOVE) return NO_MOVE;

        // Standing on a cell is always illegal
        if (move.from == move.to) return ILLEGAL_MOVE;

        // From and to must be valid cells
        if (!move.from.isInFieldBounds() || !move.to.isInFieldBounds()) return ILLEGAL_MOVE;

        // Moving from a house is illegal
        if ((*this)[move.from].hasHouse) return ILLEGAL_MOVE;

        const bool targetIsHouse = (*this)[move.to].hasHouse;

        // Moving to occupied house is illegal
        if (targetIsHouse && (*this)[move.to].entity.type != Entity::NONE_TYPE) return ILLEGAL_MOVE;

        const Entity::EntityType entityType = (*this)[move.from].entity.type;
        // Entity on cell from must exist
        if (entityType == Entity::NONE_TYPE) return ILLEGAL_MOVE;

        const int player = (*this)[move.from].entity.ownerId,
                enemy = (player + 1) % 2;

        const Cell enemyTrainerCell = positions.at(Entity::idOf(enemy, Entity::TRAINER));

        const bool enemyTrainerActive = activeEntities.count(Entity::idOf(enemy, Entity::TRAINER));

        // check against from or to cells are blocked by enemy trainer
        if (enemyTrainerActive) {
            if (isBlockedByTrainer(move.from, enemyTrainerCell)) return ILLEGAL_MOVE;
            if (isBlockedByTrainer(move.to, enemyTrainerCell)) return ILLEGAL_MOVE;
        }

        const int difRow = move.to.row - move.from.row,
                difCol = move.to.col - move.from.col;

        // Base move
        if ((*this)[move.to].entity.type == Entity::NONE_TYPE) {
            if (targetIsHouse) {
                if (abs(difCol) + abs(difRow) == 1) return BASE_MOVE;
            } else {
                if (abs(difRow) <= 1 && abs(difCol) <= 1) return BASE_MOVE;
            }
        }

        // For magician
        const Entity targetEntity = (*this)[move.to].entity;
        // For strongman
        const Cell nextCell{move.to.row + difRow,
                            move.to.col + difCol};

        switch (entityType) {
            case Entity::CLOWN:
            case Entity::TRAINER:
            case Entity::NONE_TYPE:
                // Clowns and trainers can't do any special move; none ... is none, isn't it?
                break;
            case Entity::ACROBAT:
                // Double move
                if ((*this)[move.to].entity.type == Entity::NONE_TYPE) {
                    // Vertical/horizontal
                    if ((difCol == 0 || difRow == 0) && abs(difCol) + abs(difRow) == 2) return DOUBLE_MOVE;

                    // Diagonal
                    if (!targetIsHouse) {
                        if (abs(difRow) == 2 && abs(difCol) == 2) return DOUBLE_MOVE;
                    }
                }
                break;
            case Entity::STRONGMAN:
                // Strongmen can push other entities
                if (nextCell.isInFieldBounds() && (*this)[nextCell].entity.type == Entity::NONE_TYPE
                    && (!(*this)[nextCell].hasHouse || (difCol == 0 || difRow == 0))
                    && (!enemyTrainerActive || !isBlockedByTrainer(nextCell, enemyTrainerCell)))
                    return PUSH;
                break;
            case Entity::MAGICIAN:
                // Magicians can use 'teleportation'
                if ( // 'Teleportation' is not a real teleportation but rather a swap with any other entity
                        targetEntity.type != Entity::NONE_TYPE
                        && (    // ... excluding enemy trainer and magician
                                targetEntity.ownerId == player || targetEntity.type != Entity::TRAINER
                                                                  && targetEntity.type != Entity::MAGICIAN)
                        )
                    return SWAP;
                break;
        }

        // Move doesn't match any pattern, so it is illegal
        return ILLEGAL_MOVE;
    }

    void doMove(const Move move) {
        switch (checkMove(move)) {
            case ILLEGAL_MOVE:


                break;
            case NO_MOVE:
                // Do nothing

                break;
            case BASE_MOVE:
            case DOUBLE_MOVE:

                baseOrDoubleMove(move);
                break;
            case SWAP:

                swapMove(move);
                break;
            case PUSH:

                pushMove(move);
                break;
        }
    }

    /**
     * Checks if @param cell is blocked by trainer on @param trainerCell.
     * @return -1 if cell == trainerCell, 1 if cell is blocked, 0 otherwise
     */
    static bool isBlockedByTrainer(const Cell cell, const Cell trainerCell) {
        const int dstRow = abs(cell.row - trainerCell.row),
                dstCol = abs(cell.col - trainerCell.col);

        return dstRow <= 1 && dstCol <= 1;
    }

private:
    void baseOrDoubleMove(const Move move) {
        Entity movingEntity = (*this)[move.from].entity;

        clear(move.from);
        set(move.to, movingEntity);

        CellInfo info = (*this)[move.to];

        if (info.hasHouse) {
            activeEntities.erase(movingEntity.id);
            freeHouses.erase(move.to);
        }
    }

    void swapMove(const Move move) {
        Entity magician = (*this)[move.from].entity;
        Entity assistant = (*this)[move.to].entity;

        set(move.to, magician);
        set(move.from, assistant);
    }

    void pushMove(const Move move) {
        Entity strongman = (*this)[move.from].entity;
        Entity pushedEntity = (*this)[move.to].entity;

        // nextCell = to + (to - from)
        Cell nextCell{2 * move.to.row - move.from.row, 2 * move.to.col - move.from.col};

        clear(move.from);
        set(move.to, strongman);
        set(nextCell, pushedEntity);

        CellInfo info = (*this)[nextCell];

        if (info.hasHouse) {
            activeEntities.erase(pushedEntity.id);
            freeHouses.erase(nextCell);
        }
    }

};

struct State {
    /*const*/ int myPlayer = -1;

    Field field;

    int doneSteps = 0;
    int currentPlayer = 0;

    void doMove(const Move move) {
        field.doMove(move);

        currentPlayer = (currentPlayer + 1) % 2;
        doneSteps++;
    }
};

/******************************************** game I/O ****************************************************************/

istream &operator>>(istream &in, Cell &cell) {


    string str;
    in >> str;

    cell.col = str[0] - 'A';
    cell.row = str[1] - '1';



    return in;
}

ostream &operator<<(ostream &out, const Cell cell) {
    out << (char) (cell.col + 'A') << (char) (cell.row + '1');
    return out;
}

istream &operator>>(istream &in, Move &move) {


    string str;
    in >> str;

    move.from.col = str[0] - 'A';
    move.from.row = str[1] - '1';

    move.to.col = str[3] - 'A';
    move.to.row = str[4] - '1';



    return in;
}

ostream &operator<<(ostream &out, const Move move) {
    out << move.from << "-" << move.to;
    return out;
}

int rowForPlayer(int col, int player) {
    if (player == 0) return col;
    else return FIELD_HEIGHT - 1 - col;
}

void initializeEntities(Field &field, int player) {
    field.set(rowForPlayer(0, player), 0, Entity(player, Entity::ACROBAT));
    field.set(rowForPlayer(1, player), 0, Entity(player, Entity::CLOWN));
    field.set(rowForPlayer(0, player), 1, Entity(player, Entity::CLOWN, true));
    field.set(rowForPlayer(1, player), 1, Entity(player, Entity::MAGICIAN));
    field.set(rowForPlayer(2, player), 0, Entity(player, Entity::STRONGMAN));
    field.set(rowForPlayer(0, player), 2, Entity(player, Entity::STRONGMAN, true));
    field.set(rowForPlayer(3, player), 0, Entity(player, Entity::TRAINER));
}

istream &operator>>(istream &in, State &state) {

    for (int i = 0; i < 13 /* houses count */; ++i) {
        Cell c;
        cin >> c;
        state.field.houses.insert(c);
        state.field.freeHouses.insert(c);
        state.field[c].hasHouse = true;
    }

    in >> state.myPlayer;

    for (int i = 0; i < 0b111 /* TRAINER + 1 */; ++i) {
        state.field.activeEntities.insert(i);
        state.field.activeEntities.insert(i | 0b1000);
    }

    initializeEntities(state.field, 0);
    initializeEntities(state.field, 1);

    return in;
}

/******************************************** main ********************************************************************/

void mainLoop(State &);

Move doMove(const State &);

int main() {


    State state;
    cin >> state;

    while (state.doneSteps < MAX_STEPS && !state.field.freeHouses.empty())
        mainLoop(state);


    return 0;
}


void mainLoop(State &state) {
    if (state.currentPlayer != state.myPlayer) {
        Move move;
        cin >> move;
        state.doMove(move);
    } else {
        Move move = doMove(state);
        state.doMove(move);
        cout << move << endl;
    }
}

/******************************************** doMove and helpers ******************************************************/

inline void addMoveIfLegal(const State &state, vector<Move> &out, const Move &move, const bool addSwaps = false) {
    switch (state.field.checkMove(move)) {
        case Field::BASE_MOVE:
        case Field::PUSH:
        case Field::DOUBLE_MOVE:
        case Field::NO_MOVE:
            out.push_back(move);
            break;

        case Field::SWAP:
            if (addSwaps) out.push_back(move);
            break;

        case Field::ILLEGAL_MOVE:
            // Don't add
            break;
    }
}

vector<Move> allAvailableMoves(const State &state) {
    vector<Move> res;

    // Base move, push (strongman)
    for (const int entityId : state.field.activeEntities) {
        const Cell position = state.field.positions.at(entityId);
        const Entity entity(entityId);
        if (entity.ownerId != state.currentPlayer) continue;

        for (int dRow = -1; dRow <= 1; ++dRow) {
            for (int dCol = -1; dCol <= 1; ++dCol) {
                const Move move{position, {position.row + dRow, position.col + dCol}};
                addMoveIfLegal(state, res, move);
            }
        }
    }

    Cell position;

    // Double move (acrobat)
    position = state.field.positions.at(Entity::idOf(state.currentPlayer, Entity::ACROBAT));
    for (int dRow = -1; dRow <= 1; ++dRow) {
        for (int dCol = -1; dCol <= 1; ++dCol) {
            const Move move{position, {position.row + dRow, position.col + dCol}};
            addMoveIfLegal(state, res, move);
        }
    }

    // Swap (magician)
    position = state.field.positions.at(Entity::idOf(state.currentPlayer, Entity::MAGICIAN));
    for (const int assistantId : state.field.activeEntities) {
        const Cell assistantPosition = state.field.positions.at(assistantId);
        addMoveIfLegal(state, res, {position, assistantPosition});
    }

    // No move
    res.push_back(NONE_MOVE);

    return res;
}

int distanceToNearestHouse(const State &state, const Cell &cell) {
    int dst = 1000;
    for (auto house : state.field.freeHouses) {
        dst = min(dst, abs(cell.row - house.row) + abs(cell.col - house.col));
    }
    if (dst == 1000) dst = 0;

    return dst;
}

int distanceToNearestHouse(const State &state, const Entity &entity) {
    return distanceToNearestHouse(state, state.field.positions.at(entity.id));
}

int stateScore(const State &state) {

    int score = 0;

    const int player = state.myPlayer,
            enemy = (player + 1) % 2;

    const Cell friendTrainerCell = state.field.positions.at(Entity::idOf(player, Entity::TRAINER)),
            enemyTrainerCell = state.field.positions.at(Entity::idOf(enemy, Entity::TRAINER));

    const bool friendTrainerActive = state.field.activeEntities.count(Entity::idOf(player, Entity::TRAINER)) == 1,
            enemyTrainerActive = state.field.activeEntities.count(Entity::idOf(enemy, Entity::TRAINER)) == 1;

    // Macroses for checking if cell is blocked by a trainer. You can think that they are local functions
#define isBlockedByFriendTrainer(cell) \
friendTrainerActive && Field::isBlockedByTrainer(friendTrainerCell, cell) && !state.field[cell].hasHouse
#define isBlockedByEnemyTrainer(cell) \
enemyTrainerActive && Field::isBlockedByTrainer(enemyTrainerCell, cell) && !state.field[cell].hasHouse

    for (int entityId = 0; entityId < 15; ++entityId) {
        // Entity with id 7 doesn't exist
        if (entityId == 7) continue;

        const Entity entity(entityId);
        const bool my = entity.ownerId == player;
        const Cell cell = state.field.positions.at(entityId);

        // Score for houses
        if (state.field[cell].hasHouse) {
            if (my) score += SCORE_FOR_CAPTURED_HOUSE;
            else score += SCORE_FOR_LOST_HOUSE;

            continue;
        }

        // Score for entities and trainer blocks
        switch (entity.type) {
            case Entity::CLOWN:
                if (my) {
                    score += SCORE_FOR_UNINHABITED_FRIEND_CLOWN;
                    if (isBlockedByEnemyTrainer(cell)) score += SCORE_FOR_BLOCKED_FRIEND_CLOWN;
                } else {
                    score += SCORE_FOR_UNINHABITED_ENEMY_CLOWN;
                    if (isBlockedByFriendTrainer(cell)) score += SCORE_FOR_BLOCKED_ENEMY_CLOWN;
                }
                break;

            case Entity::STRONGMAN:
                if (my) {
                    score += SCORE_FOR_UNINHABITED_FRIEND_STRONGMAN;
                    if (isBlockedByEnemyTrainer(cell)) score += SCORE_FOR_BLOCKED_FRIEND_STRONGMAN;
                } else {
                    score += SCORE_FOR_UNINHABITED_ENEMY_STRONGMAN;
                    if (isBlockedByFriendTrainer(cell)) score += SCORE_FOR_BLOCKED_ENEMY_STRONGMAN;
                }
                break;

            case Entity::ACROBAT:
                if (my) {
                    score += SCORE_FOR_UNINHABITED_FRIEND_ACROBAT;
                    if (isBlockedByEnemyTrainer(cell)) score += SCORE_FOR_BLOCKED_FRIEND_ACROBAT;
                } else {
                    score += SCORE_FOR_UNINHABITED_ENEMY_ACROBAT;
                    if (isBlockedByFriendTrainer(cell)) score += SCORE_FOR_BLOCKED_ENEMY_ACROBAT;
                }
                break;

            case Entity::MAGICIAN:
                if (my) {
                    score += SCORE_FOR_UNINHABITED_FRIEND_MAGICIAN;
                    if (isBlockedByEnemyTrainer(cell)) score += SCORE_FOR_BLOCKED_FRIEND_MAGICIAN;
                } else {
                    score += SCORE_FOR_UNINHABITED_ENEMY_MAGICIAN;
                    if (isBlockedByFriendTrainer(cell)) score += SCORE_FOR_BLOCKED_ENEMY_MAGICIAN;
                }
                break;

            case Entity::TRAINER:
                // Trainers can't block each other
                if (my) score -= SCORE_FOR_UNINHABITED_FRIEND_TRAINER;
                else score -= SCORE_FOR_UNINHABITED_ENEMY_TRAINER;
                break;

            case Entity::NONE_TYPE:
                break;
        }

        // Score for distances
        if (my) {
            score -= SCORE_DISTANCE_TO_END_MULTIPLIER * (11 - cell.col);
        } else {
            score += SCORE_DISTANCE_TO_END_MULTIPLIER * (11 - cell.col);
        }

        int dst = distanceToNearestHouse(state, cell);

        if (my) {
            score -= SCORE_DISTANCE_TO_HOUSE_MULTIPLIER * dst;
        } else {
            score += SCORE_DISTANCE_TO_HOUSE_MULTIPLIER * dst;
        }
    }

    return score;

    // Undefine macroses at function's end
#undef isBlockedByEnemyTrainer
#undef isBlockedByFriendTrainer
}

pair<int, Move> chooseBestMoveRecursive(const State &state, int depth) {
    State tmp = state;
    vector<Move> allMoves = allAvailableMoves(state);
    vector<pair<int, Move>> movesWithScore;

    for (Move move : allMoves) {
        tmp.doMove(move);

        int score;
        score = stateScore(tmp);

        movesWithScore.emplace_back(score, move);

        tmp = state;
    }

    sort(movesWithScore.begin(), movesWithScore.end(),
         [](const pair<int, Move> &left, const pair<int, Move> &right) { return left.first < right.first; });

    if (state.currentPlayer == state.myPlayer) {
        int minScore = movesWithScore.back().first - 50;
        while (movesWithScore.front().first < minScore) {
            movesWithScore.erase(movesWithScore.begin());
        }
    } else {
        int maxScore = movesWithScore.back().first + 50;
        while (movesWithScore.back().first > maxScore) {
            movesWithScore.pop_back();
        }
    }

    if (depth > 0) {
        for (auto &move : movesWithScore) {
            tmp.doMove(move.second);

            move.first = chooseBestMoveRecursive(tmp, depth - 1).first;

            tmp = state;
        }
    }

    sort(movesWithScore.begin(), movesWithScore.end(),
         [](const pair<int, Move> &left, const pair<int, Move> &right) { return left.first < right.first; });

    if (state.currentPlayer == state.myPlayer) return movesWithScore.back();
    else return movesWithScore.front();
}

Move doMove(const State &state) {

    int movesCount = allAvailableMoves(state).size();
    int depth = floor(log(200.0) / log(movesCount * 1.0));



    Entity acrobat = Entity(state.myPlayer, Entity::ACROBAT);
    Entity magician = Entity(state.myPlayer, Entity::MAGICIAN);
    Entity clown1 = Entity(state.myPlayer, Entity::CLOWN, false);
    Entity clown2 = Entity(state.myPlayer, Entity::CLOWN, true);

    if (distanceToNearestHouse(state, acrobat) <= 2 && distanceToNearestHouse(state, magician) > 2) {
        const Move move = Move{
                state.field.positions.at(magician.id),
                state.field.positions.at(acrobat.id)};

        if (state.field.checkMove(move)) return move;
    }

    if (distanceToNearestHouse(state, magician) <= 2) {
        if (distanceToNearestHouse(state, clown1) > 2) {
            const Move move = Move{
                    state.field.positions.at(magician.id),
                    state.field.positions.at(clown1.id)};

            if (state.field.checkMove(move)) return move;
        }
        if (distanceToNearestHouse(state, clown2) > 2) {
            const Move move = Move{
                    state.field.positions.at(magician.id),
                    state.field.positions.at(clown2.id)};

            if (state.field.checkMove(move)) return move;
        }
    }

    auto moveInfo = chooseBestMoveRecursive(state, depth);


    return moveInfo.second;
}
