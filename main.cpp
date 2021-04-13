#include <iostream>
#include <cmath>
#include <chrono>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std;
using namespace chrono;

static constexpr int MAX_STEPS = 300;

static constexpr int FIELD_WIDTH = 12;
static constexpr int FIELD_HEIGHT = 9;

#ifdef RELEASE
#define abortDebug()
#else
#define abortDebug() abort()
#endif

/******************************************** logging *****************************************************************/
#ifdef LOCAL_RUN

#include <fstream>

struct LogObj {
    ofstream out = ofstream("log.txt");

    ~LogObj() {
        out.close();
    }
};

template<typename T>
inline LogObj &operator<<(LogObj &out, const T &val) {
    out.out << val;
    return out;
}

#elif RELEASE

struct LogObj {
};

template<typename T>
inline LogObj &operator<<(LogObj &out, const T &val) {
    return out;
}

#else

struct LogObj {
};

template<typename T>
inline LogObj &operator<<(LogObj &out, const T &val) {
    cerr << val;
    return out;
}

#endif

inline LogObj &operator<<(LogObj &s, LogObj &(*pf)(LogObj &)) {
    return pf(s);
}

const auto startPoint = chrono::steady_clock::now();

inline LogObj &now(LogObj &out) {
    out << chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - startPoint).count() << "ms";
    return out;
}

inline LogObj &endl(LogObj &out) {
    out.out << endl;
    return out;
}

inline LogObj &logHeader(LogObj &out) {
    out << now << "\t";
    return out;
}

inline LogObj &logErr(LogObj &out) {
    out << logHeader << "err" << "\t";
    return out;
}

inline LogObj &logWarn(LogObj &out) {
    out << logHeader << "warn" << "\t";
    return out;
}

inline LogObj &logInfo(LogObj &out) {
    out << logHeader << "info" << "\t";
    return out;
}

inline LogObj &logVerb(LogObj &out) {
    out << logHeader << "verb" << "\t";
    return out;
}

LogObj lout;


/******************************************** game structures *********************************************************/

struct Cell {
    int row = 25, col = -1;

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

enum EntityType {
    CLOWN = 0,      // 0b000
    STRONGMAN = 2,  // 0b010
    ACROBAT = 4,    // 0b100
    MAGICIAN = 5,   // 0b101
    TRAINER = 6,    // 0b110
    NONE = -1,
};

struct Entity {
    /* const */ int id;
    /* const */ int ownerId;
    /* const */ EntityType type;

    static int idOf(const int ownerId, const EntityType type, bool isSecond = false) {
        return (ownerId << 3) + (int) type + (int) isSecond;
    }

    Entity(const int ownerId, const EntityType type, bool isSecond = false) :
            id(idOf(ownerId, type, isSecond)),
            ownerId(ownerId),
            type(type) {}
};


const Entity NONE_ENTITY(-1, NONE);

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
        DISALLOWED,
        NO_MOVE,
        BASE,
        DOUBLE,
        SWAP,
        PUSH,
    };

    MoveType checkMove(const Move move) {
        // NONE_MOVE is always allowed
        if (move == NONE_MOVE) return NO_MOVE;

        // Standing on a cell is always disallowed
        if (move.from == move.to) return DISALLOWED;

        // From and to must be valid cells
        if (!move.from.isInFieldBounds() || !move.to.isInFieldBounds()) return DISALLOWED;

        // Moving from a house is disallowed
        if ((*this)[move.from].hasHouse) return DISALLOWED;

        const bool targetIsHouse = (*this)[move.to].hasHouse;

        // Moving to occupied house is disallowed
        if (targetIsHouse && (*this)[move.to].entity.type != NONE) return DISALLOWED;

        const EntityType entityType = (*this)[move.from].entity.type;
        // Entity on cell from must exist
        if (entityType == NONE) return DISALLOWED;

        const int player = (*this)[move.from].entity.ownerId,
                enemy = player % 2 + 1;

        const Cell enemyTrainerCell = positions[Entity::idOf(enemy, TRAINER)];

        const bool enemyTrainerActive = activeEntities.count(Entity::idOf(enemy, TRAINER));

        // isInFieldBounds against from or to cells are blocked by enemy trainer
        if (enemyTrainerActive) {
            if (isBlockedByTrainer(move.from, enemyTrainerCell)) return DISALLOWED;
            if (isBlockedByTrainer(move.to, enemyTrainerCell)) return DISALLOWED;
        }

        const int difRow = move.to.row - move.from.row,
                difCol = move.to.col - move.from.col;

        // Base doMove
        if ((*this)[move.to].entity.type == NONE) {
            if (targetIsHouse) {
                if (abs(difCol) + abs(difRow) == 1) return BASE;
            } else {
                if (abs(difRow) <= 1 && abs(difCol) <= 1) return BASE;
            }
        }

        // For magician
        const Entity targetEntity = (*this)[move.to].entity;
        // For strongman
        const Cell nextCell{move.to.row + 2 * difRow,
                            move.to.col + 2 * difCol};

        switch (entityType) {
            case CLOWN:
            case TRAINER:
            case NONE:
                // Clowns and trainers can't do any special doMove; none ... is none, isn't it?
                break;
            case ACROBAT:
                // Double doMove
                if ((*this)[move.to].entity.type == NONE) {
                    // Vertical/horizontal
                    if ((difCol == 0 || difRow == 0) && abs(difCol) + abs(difRow) == 2) return DOUBLE;

                    // Diagonal
                    if (!targetIsHouse) {
                        if (abs(difRow) == 2 && abs(difCol) == 2) return DOUBLE;
                    }
                }
                break;
            case STRONGMAN:
                // Strongmen can push other entities
                if (nextCell.isInFieldBounds() && (*this)[nextCell].entity.type == NONE
                    && (!(*this)[nextCell].hasHouse || (difCol == 0 || difRow == 0))
                    && (!enemyTrainerActive || !isBlockedByTrainer(nextCell, enemyTrainerCell)))
                    return PUSH;
                break;
            case MAGICIAN:
                // Magicians can use 'teleportation'
                if ( // 'Teleportation' is not a real teleportation but rather a swap with any other entity
                        targetEntity.type != NONE
                        && (    // ... excluding enemy trainer and magician
                                targetEntity.ownerId == player || targetEntity.type != TRAINER
                                                                  && targetEntity.type != MAGICIAN)
                        )
                    return SWAP;
                break;
        }

        // Move doesn't match any pattern, so it is disallowed
        return DISALLOWED;
    }

    void doMove(const Move move) {
        lout << logVerb << "doing move " << move << "..." << endl;

        switch (checkMove(move)) {
            case DISALLOWED:
                lout << logErr << "illegal move " << move << endl;
                abortDebug();
                break;
            case NO_MOVE:
                // Do nothing
                break;
            case BASE:
            case DOUBLE:
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

    /**
     * Checks if @param cell is blocked by trainer on @param trainerCell.
     * @return -1 if cell == trainerCell, 1 if cell is blocked, 0 otherwise
     */
    static bool isBlockedByTrainer(const Cell cell, const Cell trainerCell) {
        const int dstRow = abs(cell.row - trainerCell.row),
                dstCol = abs(cell.col - trainerCell.col);

        return dstRow <= 1 && dstCol <= 1;
    }
};

struct State {
    /*const*/ int myPlayer = -1;

    Field field;

    int doneSteps = 0;
    int currentPlayer = 0;
};

/******************************************** game I/O ****************************************************************/

istream &operator>>(istream &in, Cell &cell) {
    lout << logVerb << "reading a cell..." << endl;

    string str;
    in >> str;

    cell.row = str[0] - 'A';
    cell.col = str[1] - '1';

    lout << logVerb << "cell '" << cell << "' was read" << endl;

    return in;
}

ostream &operator<<(ostream &out, const Cell cell) {
    out << (char) (cell.row + 'A') << (char) (cell.col + '1');
    return out;
}

istream &operator>>(istream &in, Move &move) {
    lout << logVerb << "reading a doMove..." << endl;

    string str;
    in >> str;

    move.from.row = str[0] - 'A';
    move.from.col = str[1] - '1';

    if (str[2] != '-') lout << logErr << "unexpected symbol when reading doMove: '" << str << "'" << endl;

    move.to.row = str[3] - 'A';
    move.to.col = str[4] - '1';

    lout << logVerb << "doMove '" << move << "' was read" << endl;

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
    field.set(rowForPlayer(0, player), 0, Entity(player, ACROBAT));
    field.set(rowForPlayer(1, player), 0, Entity(player, CLOWN));
    field.set(rowForPlayer(0, player), 1, Entity(player, CLOWN));
    field.set(rowForPlayer(1, player), 1, Entity(player, MAGICIAN));
    field.set(rowForPlayer(2, player), 0, Entity(player, STRONGMAN));
    field.set(rowForPlayer(0, player), 2, Entity(player, STRONGMAN));
    field.set(rowForPlayer(3, player), 0, Entity(player, TRAINER));
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
    lout << logInfo << "starting" << endl;

    State state;
    cin >> state;

    while (state.doneSteps < MAX_STEPS)
        mainLoop(state);


    return 0;
}


void mainLoop(State &state) {
    if (state.currentPlayer != state.myPlayer) {
        Move move;
        cin >> move;
        state.field.doMove(move);
    } else {
        Move move = doMove(state);
        state.field.doMove(move);
        cout << move << endl;
    }

    state.doneSteps++;
    state.currentPlayer = (state.currentPlayer + 1) % 2;
}

Move doMove(const State &state) {
    //TODO
    return NONE_MOVE;
}