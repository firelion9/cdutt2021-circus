#include <iostream>
#include <cmath>
#include <chrono>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std;
using namespace chrono;

constexpr int MAX_STEPS = 300;

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
inline LogObj& operator<<(LogObj &out, const T &val) {
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
};

template<>
struct std::hash<Cell> {
    size_t operator()(const Cell& cell) const {
        return ((size_t)cell.row << 32) + cell.col;
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

    Entity(const int ownerId, const EntityType type, bool isSecond = false) :
            id((ownerId << 3) + (int) type + (int) isSecond),
            ownerId(ownerId),
            type(type) {}
};


const Entity NONE_ENTITY(-1, NONE);

struct CellInfo {
    /*const*/ bool hasHouse = false;
    Entity entity = NONE_ENTITY;
};

static constexpr int FIELD_WIDTH = 12;
static constexpr int FIELD_HEIGHT = 9;

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

        if (positions.count(entity.id) != 0) (*this)[positions[entity.id]].entity = NONE_ENTITY;
        positions[entity.id] = cell;
    }

    bool checkMove(const Move move) {
        //TODO
        return true;
    }

    void move(const Move move) {
        if (move == NONE_MOVE) return;

        Entity e = (*this)[move.from].entity;

        set(move.to, e);
        CellInfo info = (*this)[move.to];

        if (info.hasHouse) {
            activeEntities.erase(e.id);
            freeHouses.erase(move.to);
        }
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
    out << (char)(cell.row + 'A') << (char)(cell.col + '1');
    return out;
}

istream &operator>>(istream &in, Move &move) {
    lout << logVerb << "reading a move..." << endl;

    string str;
    in >> str;

    move.from.row = str[0] - 'A';
    move.from.col = str[1] - '1';

    if (str[2] != '-') lout << logErr << "unexpected symbol when reading move: '" << str << "'" << endl;

    move.to.row = str[3] - 'A';
    move.to.col = str[4] - '1';

    lout << logVerb << "move '" << move << "' was read" << endl;

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

void mainLoop(State&);
Move doMove(const State&);

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
        state.field.move(move);
    } else {
        Move move = doMove(state);
        state.field.move(move);
        cout << move << endl;
    }

    state.doneSteps++;
    state.currentPlayer = (state.currentPlayer + 1) % 2;
}

Move doMove(const State &state) {
    //TODO
    return NONE_MOVE;
}