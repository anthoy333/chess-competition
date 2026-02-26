#include "chess-simulator.h"
#include "chess.hpp"
#include <unordered_map>
#include <chrono>
#include <algorithm>
#include <random>
#include <array>

using namespace ChessSimulator;

static constexpr int INF = 1000000;
static constexpr int MATE_SCORE = 100000;
static constexpr int MAX_DEPTH = 5;

static std::chrono::high_resolution_clock::time_point searchStart;
static int timeLimitMsGlobal;

bool outOfTime() {
    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - searchStart).count();
    return elapsed >= timeLimitMsGlobal;
}


enum NodeType { EXACT, LOWERBOUND, UPPERBOUND };

struct TTEntry {
    int depth;
    int score;
    NodeType type;
};


static constexpr int TT_SIZE = 1 << 22; // 4M entries (~64MB)
static TTEntry transTable[TT_SIZE];

inline TTEntry* probeTT(uint64_t hash)
{
    return &transTable[hash & (TT_SIZE - 1)];
}

inline void storeTT(uint64_t hash, int depth, int score, NodeType type)
{
    TTEntry* entry = probeTT(hash);

    if (entry->depth <= depth)
    {
        entry->depth = depth;
        entry->score = score;
        entry->type  = type;
    }
}

inline void clearTT()
{
    for (int i = 0; i < TT_SIZE; ++i)
        transTable[i].depth = -1;
}

static chess::Move killerMoves[128][2];
static int historyHeuristic[2][64][64];

static const int knightPST[64] = {
    -50,-40,-30,-30,-30,-30,-40,-50,
    -40,-20,  0,  0,  0,  0,-20,-40,
    -30,  0, 10, 15, 15, 10,  0,-30,
    -30,  5, 15, 20, 20, 15,  5,-30,
    -30,  0, 15, 20, 20, 15,  0,-30,
    -30,  5, 10, 15, 15, 10,  5,-30,
    -40,-20,  0,  5,  5,  0,-20,-40,
    -50,-40,-30,-30,-30,-30,-40,-50
 };

static const int pawnPST[64] = {
    0,  0,  0,  0,  0,  0,  0,  0,
    5, 10, 10,-20,-20, 10, 10,  5,
    5, -5,-10,  0,  0,-10, -5,  5,
    0,  0,  0, 20, 20,  0,  0,  0,
    5,  5, 10, 25, 25, 10,  5,  5,
   10, 10, 20, 30, 30, 20, 10, 10,
   50, 50, 50, 50, 50, 50, 50, 50,
    0,  0,  0,  0,  0,  0,  0,  0
};

static const int bishopPST[64] = {
    -20,-10,-10,-10,-10,-10,-10,-20,
    -10,  5,  0,  0,  0,  0,  5,-10,
    -10, 10, 10, 10, 10, 10, 10,-10,
    -10,  0, 10, 10, 10, 10,  0,-10,
    -10,  5,  5, 10, 10,  5,  5,-10,
    -10,  0,  5, 10, 10,  5,  0,-10,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -20,-10,-10,-10,-10,-10,-10,-20
 };
static const int rookPST[64] = {
    0,  0,  5, 10, 10,  5,  0,  0,
   -5,  0,  0,  0,  0,  0,  0, -5,
   -5,  0,  0,  0,  0,  0,  0, -5,
   -5,  0,  0,  0,  0,  0,  0, -5,
   -5,  0,  0,  0,  0,  0,  0, -5,
   -5,  0,  0,  0,  0,  0,  0, -5,
    5, 10, 10, 10, 10, 10, 10,  5,
    0,  0,  5, 10, 10,  5,  0,  0
};
static const int queenPST[64] = {
    -20,-10,-10, -5, -5,-10,-10,-20,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -10,  0,  5,  5,  5,  5,  0,-10,
     -5,  0,  5,  5,  5,  5,  0, -5,
      0,  0,  5,  5,  5,  5,  0, -5,
    -10,  5,  5,  5,  5,  5,  0,-10,
    -10,  0,  5,  0,  0,  0,  0,-10,
    -20,-10,-10, -5, -5,-10,-10,-20
};
int pieceValue(chess::Piece::underlying p) {
    switch (p) {
        case chess::Piece::underlying::WHITEPAWN:
        case chess::Piece::underlying::BLACKPAWN: return 100;
        case chess::Piece::underlying::WHITEKNIGHT:
        case chess::Piece::underlying::BLACKKNIGHT: return 320;
        case chess::Piece::underlying::WHITEBISHOP:
        case chess::Piece::underlying::BLACKBISHOP: return 330;
        case chess::Piece::underlying::WHITEROOK:
        case chess::Piece::underlying::BLACKROOK: return 500;
        case chess::Piece::underlying::WHITEQUEEN:
        case chess::Piece::underlying::BLACKQUEEN: return 900;
        default: return 0;
    }
}
int evaluateMaterial(chess::Board& board)
{
    int score = 0;

    for (int i = 0; i < 64; ++i)
    {
        auto piece = board.at(chess::Square(i)).internal();
        if (piece == chess::Piece::underlying::NONE)
            continue;

        int val = pieceValue(piece);

        if (chess::Piece(piece).color() == chess::Color::WHITE)
            score += val;
        else
            score -= val;
    }

    return score;
}
int evaluateMobility(chess::Board& board)
{
    chess::Movelist whiteMoves;
    chess::movegen::legalmoves(whiteMoves, board);

    int whiteMob = (board.sideToMove() == chess::Color::WHITE)
                   ? whiteMoves.size()
                   : 0;

    board.makeNullMove();

    chess::Movelist blackMoves;
    chess::movegen::legalmoves(blackMoves, board);

    int blackMob = (board.sideToMove() == chess::Color::BLACK)
                   ? blackMoves.size()
                   : 0;

    board.unmakeNullMove();

    return (whiteMob - blackMob) * 2;  // small weight
}
int evaluateKingSafety(chess::Board& board)
{
    int score = 0;

    for (int i = 0; i < 64; i++)
    {
        auto piece = board.at(chess::Square(i)).internal();
        if (piece == chess::Piece::underlying::WHITEKING)
        {
            int file = i % 8;
            int rank = i / 8;

            for (int df = -1; df <= 1; df++)
            {
                int f = file + df;
                int r = rank + 1;

                if (f >= 0 && f < 8 && r < 8)
                {
                    int sq = r * 8 + f;
                    if (board.at(chess::Square(sq)).internal() ==
                        chess::Piece::underlying::WHITEPAWN)
                        score += 15;
                }
            }
        }

        if (piece == chess::Piece::underlying::BLACKKING)
        {
            int file = i % 8;
            int rank = i / 8;

            for (int df = -1; df <= 1; df++)
            {
                int f = file + df;
                int r = rank - 1;

                if (f >= 0 && f < 8 && r >= 0)
                {
                    int sq = r * 8 + f;
                    if (board.at(chess::Square(sq)).internal() ==
                        chess::Piece::underlying::BLACKPAWN)
                        score -= 15;
                }
            }
        }
    }

    return score;
}
int evaluatePawnStructure(chess::Board& board)
{
    int score = 0;

    for (int file = 0; file < 8; file++)
    {
        int whiteCount = 0;
        int blackCount = 0;

        for (int rank = 0; rank < 8; rank++)
        {
            int sq = rank * 8 + file;
            auto piece = board.at(chess::Square(sq)).internal();

            if (piece == chess::Piece::underlying::WHITEPAWN)
                whiteCount++;
            if (piece == chess::Piece::underlying::BLACKPAWN)
                blackCount++;
        }

        if (whiteCount > 1)
            score -= 15 * (whiteCount - 1);

        if (blackCount > 1)
            score += 15 * (blackCount - 1);
    }

    // Passed pawns
    for (int i = 0; i < 64; i++)
    {
        auto piece = board.at(chess::Square(i)).internal();
        if (piece == chess::Piece::underlying::WHITEPAWN)
        {
            int file = i % 8;
            int rank = i / 8;

            bool passed = true;

            for (int r = rank + 1; r < 8 && passed; r++)
            {
                for (int f = file - 1; f <= file + 1; f++)
                {
                    if (f >= 0 && f < 8)
                    {
                        int sq = r * 8 + f;
                        if (board.at(chess::Square(sq)).internal() ==
                            chess::Piece::underlying::BLACKPAWN)
                        {
                            passed = false;
                            break;
                        }
                    }
                }
            }

            if (passed)
                score += (rank * 10);
        }

        if (piece == chess::Piece::underlying::BLACKPAWN)
        {
            int file = i % 8;
            int rank = i / 8;

            bool passed = true;

            for (int r = rank - 1; r >= 0 && passed; r--)
            {
                for (int f = file - 1; f <= file + 1; f++)
                {
                    if (f >= 0 && f < 8)
                    {
                        int sq = r * 8 + f;
                        if (board.at(chess::Square(sq)).internal() ==
                            chess::Piece::underlying::WHITEPAWN)
                        {
                            passed = false;
                            break;
                        }
                    }
                }
            }

            if (passed)
                score -= ((7 - rank) * 10);
        }
    }

    return score;
}
int evaluatePieceSquare(chess::Board& board)
{
    int score = 0;
    int whiteBishops = 0;
    int blackBishops = 0;

    for (int i = 0; i < 64; i++)
    {
        auto piece = board.at(chess::Square(i)).internal();
        if (piece == chess::Piece::underlying::NONE)
            continue;

        int sq = i;
        bool isWhite = chess::Piece(piece).color() == chess::Color::WHITE;

        int pstBonus = 0;

        switch (piece)
        {
            case chess::Piece::underlying::WHITEPAWN:
            case chess::Piece::underlying::BLACKPAWN:
                pstBonus = isWhite ? pawnPST[sq] : pawnPST[63 - sq];
                break;

            case chess::Piece::underlying::WHITEKNIGHT:
            case chess::Piece::underlying::BLACKKNIGHT:
                pstBonus = isWhite ? knightPST[sq] : knightPST[63 - sq];
                break;

            case chess::Piece::underlying::WHITEBISHOP:
            case chess::Piece::underlying::BLACKBISHOP:
                pstBonus = isWhite ? bishopPST[sq] : bishopPST[63 - sq];
                if (isWhite) whiteBishops++;
                else blackBishops++;
                break;

            case chess::Piece::underlying::WHITEROOK:
            case chess::Piece::underlying::BLACKROOK:
                pstBonus = isWhite ? rookPST[sq] : rookPST[63 - sq];
                break;

            case chess::Piece::underlying::WHITEQUEEN:
            case chess::Piece::underlying::BLACKQUEEN:
                pstBonus = isWhite ? queenPST[sq] : queenPST[63 - sq];
                break;

            default:
                break;
        }

        if (isWhite)
            score += pstBonus;
        else
            score -= pstBonus;
    }

    if (whiteBishops >= 2) score += 30;
    if (blackBishops >= 2) score -= 30;

    return score;
}
int evaluateDevelopment(chess::Board& board)
{
    int score = 0;

    // Penalize undeveloped minor pieces
    if (board.at(chess::Square::SQ_B1).internal() == chess::Piece::underlying::WHITEKNIGHT)
        score -= 15;
    if (board.at(chess::Square::SQ_G1).internal() == chess::Piece::underlying::WHITEKNIGHT)
        score -= 15;

    if (board.at(chess::Square::SQ_B8).internal() == chess::Piece::underlying::BLACKKNIGHT)
        score += 15;
    if (board.at(chess::Square::SQ_G8).internal() == chess::Piece::underlying::BLACKKNIGHT)
        score += 15;

    if (board.at(chess::Square::SQ_C1).internal() == chess::Piece::underlying::WHITEBISHOP)
        score -= 15;
    if (board.at(chess::Square::SQ_F1).internal() == chess::Piece::underlying::WHITEBISHOP)
        score -= 15;

    if (board.at(chess::Square::SQ_C8).internal() == chess::Piece::underlying::BLACKBISHOP)
        score += 15;
    if (board.at(chess::Square::SQ_F8).internal() == chess::Piece::underlying::BLACKBISHOP)
        score += 15;

    return score;
}
int evaluateCenterControl(chess::Board& board)
{
    int score = 0;

    int centerSquares[4] = { 27, 28, 35, 36 }; // d4, e4, d5, e5

    for (int sq : centerSquares)
    {
        auto piece = board.at(chess::Square(sq)).internal();

        if (piece == chess::Piece::underlying::WHITEPAWN)
            score += 20;
        if (piece == chess::Piece::underlying::BLACKPAWN)
            score -= 20;
    }

    return score;
}

int evaluate(chess::Board& board)
{
    int score = 0;

    score += evaluateMaterial(board);
    score += evaluatePawnStructure(board);
    score += evaluateMobility(board);
    score += evaluateKingSafety(board);
    score += evaluatePieceSquare(board);
    score += evaluateDevelopment(board);
    score += evaluateCenterControl(board);
    return (board.sideToMove() == chess::Color::WHITE) ? score : -score;
}


int captureScore(chess::Board& board, chess::Move move) {
    auto victim = board.at(move.to()).internal();
    auto attacker = board.at(move.from()).internal();
    return pieceValue(victim) - pieceValue(attacker);
}

int scoreMove(chess::Board& board, chess::Move move, int depth) {
    if (board.isCapture(move))
        return 100000 + captureScore(board, move);

    if (move == killerMoves[depth][0])
        return 90000;

    if (move == killerMoves[depth][1])
        return 80000;

    int side = (board.sideToMove() == chess::Color::WHITE) ? 0 : 1;
    return historyHeuristic[side][move.from().index()][move.to().index()];
}

void orderMoves(chess::Movelist& moves, chess::Board& board, int depth) {
    std::sort(moves.begin(), moves.end(),
        [&](const chess::Move& a, const chess::Move& b) {
            return scoreMove(board, a, depth) >
                   scoreMove(board, b, depth);
        });
}

int quiescence(chess::Board& board, int alpha, int beta) {
    int standPat = evaluate(board);

    if (standPat >= beta)
        return beta;

    if (alpha < standPat)
        alpha = standPat;

    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);

    for (auto move : moves) {
        if (!board.isCapture(move))
            continue;

        board.makeMove(move);
        int score = -quiescence(board, -beta, -alpha);
        board.unmakeMove(move);

        if (score >= beta)
            return beta;

        if (score > alpha)
            alpha = score;
    }

    return alpha;
}

int alphaBeta(chess::Board& board, int depth, int alpha, int beta, int ply)
{
    if (outOfTime())
        return evaluate(board);

    if (depth == 0)
        return quiescence(board, alpha, beta);

    auto gameOver = board.isGameOver();
    if (gameOver.second != chess::GameResult::NONE)
    {
        if (gameOver.second == chess::GameResult::DRAW)
            return 0;
        return -MATE_SCORE + ply;
    }

    uint64_t hash = board.hash();
    TTEntry* tt = probeTT(hash);

    if (tt->depth >= depth)
    {
        if (tt->type == EXACT)
            return tt->score;
        else if (tt->type == LOWERBOUND)
            alpha = std::max(alpha, tt->score);
        else if (tt->type == UPPERBOUND)
            beta = std::min(beta, tt->score);

        if (alpha >= beta)
            return tt->score;
    }

    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);
    orderMoves(moves, board, ply);

    int originalAlpha = alpha;
    int bestScore = -INF;
    bool firstMove = true;

    for (auto move : moves)
    {
        board.makeMove(move);

        int score;

        if (firstMove)
        {
            score = -alphaBeta(board, depth - 1, -beta, -alpha, ply + 1);
            firstMove = false;
        }
        else
        {
            // PVS null window search
            score = -alphaBeta(board, depth - 1, -alpha - 1, -alpha, ply + 1);

            if (score > alpha && score < beta)
                score = -alphaBeta(board, depth - 1, -beta, -alpha, ply + 1);
        }

        board.unmakeMove(move);

        if (score > bestScore)
            bestScore = score;

        if (score > alpha)
        {
            alpha = score;

            if (!board.isCapture(move))
            {
                killerMoves[ply][1] = killerMoves[ply][0];
                killerMoves[ply][0] = move;

                int side = (board.sideToMove() == chess::Color::WHITE) ? 0 : 1;
                historyHeuristic[side][move.from().index()][move.to().index()] += depth * depth;
            }
        }

        if (alpha >= beta)
            break;
    }

    NodeType type;
    if (bestScore <= originalAlpha)
        type = UPPERBOUND;
    else if (bestScore >= beta)
        type = LOWERBOUND;
    else
        type = EXACT;

    storeTT(hash, depth, bestScore, type);

    return bestScore;
}

std::string ChessSimulator::Move(std::string fen, int timeLimitMs)
{
    chess::Board board(fen);

    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);

    if (moves.empty())
        return "";

    searchStart = std::chrono::high_resolution_clock::now();
    timeLimitMsGlobal = timeLimitMs;
    clearTT();
    chess::Move bestMove = moves[0];

    for (int depth = 1; depth <= MAX_DEPTH; depth++)
    {
        if (outOfTime())
            break;

        orderMoves(moves, board, 0);

        int alpha = -INF;
        int beta = INF;
        int bestScore = -INF;
        bool firstMove = true;

        for (auto move : moves)
        {
            board.makeMove(move);

            int score;

            if (firstMove)
            {
                score = -alphaBeta(board, depth - 1, -beta, -alpha, 1);
                firstMove = false;
            }
            else
            {
                score = -alphaBeta(board, depth - 1, -alpha - 1, -alpha, 1);

                if (score > alpha)
                    score = -alphaBeta(board, depth - 1, -beta, -alpha, 1);
            }

            board.unmakeMove(move);

            if (outOfTime())
                break;

            if (score > bestScore)
            {
                bestScore = score;
                bestMove = move;
            }

            if (score > alpha)
                alpha = score;
        }
    }

    return chess::uci::moveToUci(bestMove);
}