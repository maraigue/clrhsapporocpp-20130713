// 詰将棋を解くプログラム（OpenShogiLib利用）
// ※試しに書いたものであって、単に解くだけなら
//   OpenShogiLibのサンプルを使った方が圧倒的に速いです。
// 
// コンパイル・実行方法
// # apt-get install libosl-dev
// $ g++ -o tsume tsume.cc -losl
// $ ./tsume 6 9 2 | ./BrowserDisplay

#include <osl/mobility/kingMobility.h>

#include <osl/pieceStand.h>
#include <osl/state/simpleState.h>
#include <osl/state/numEffectState.h>
#include <osl/handicap.h>
#include <osl/move.h>
#include <osl/square.h>
#include <iostream>

#include <list>
#include <boost/shared_ptr.hpp>

// --------------------------------------------------------------
// 指し手の木構造
// MEMO: この木構造に直接NumEffectStateも突っ込んだらセグフォった
class MoveTree{
public:
    enum Status{
        simple_move, root, checkmate, cannot_checkmate, over_limit
    };
    typedef boost::shared_ptr<MoveTree> ptr;
    
private:
    osl::Move move_;
    Status status_;
    
public:
    
    std::list<ptr> children;
    MoveTree(void) : status_(root) {}
    MoveTree(Status s) : status_(s) { assert(status_ != simple_move); }
    MoveTree(osl::Move & move) : move_(move), status_(simple_move) {}
    
    static ptr createNewPtr(osl::Move & move){
        return ptr(new MoveTree(move));
    }
    
    static ptr createNewPtr(Status s){
        return ptr(new MoveTree(s));
    }
    
    osl::Move * move(void){
        if(status_ != simple_move) return NULL;
        return &move_;
    }
    
    const osl::Move * move(void) const{
        if(status_ != simple_move) return NULL;
        return &move_;
    }
    
    Status status(void) const{ return status_; }
};

// プロトタイプ宣言
int recursive_search(int max_depth, MoveTree::ptr mt, const osl::state::NumEffectState * p_nstate_orig, osl::Move * p_move, int depth);

// vが正・0・負の場合、それぞれ1・0・-1を返す
int sign(int v){
    return(v == 0 ? 0 : (v > 0 ? 1 : -1));
}

// 駒を動かしてみるためのクラス
class ActionTryMove{
private:
    MoveTree::ptr mt_;
    osl::Move move_;
    osl::state::NumEffectState * p_nstate_;
    int depth_, max_depth_;
    int valid_moves_;
    
public:
    ActionTryMove(MoveTree::ptr mt, osl::state::NumEffectState & nstate, int depth, int max_depth) : mt_(mt), p_nstate_(&nstate), depth_(depth), max_depth_(max_depth), valid_moves_(0) {}
    
    template<osl::Player pl> void doAction(const osl::Piece & pc, const osl::Square & sq){
        if(!(sq.isOnBoard()))return;
        
        // 成る
        if(osl::isPiece(pc.ptype()) && osl::canPromote(pc.ptype())){
            move_ = osl::Move(pc.square(), sq, osl::promote(pc.ptype()), p_nstate_->pieceAt(sq).ptype(), true, pl);
            if(recursive_search(max_depth_, mt_, p_nstate_, &move_, depth_ + 1) != -2){ ++valid_moves_; }
        }
        
        // 成らない
        move_ = osl::Move(pc.square(), sq, pc.ptype(), p_nstate_->pieceAt(sq).ptype(), false, pl);
        if(recursive_search(max_depth_, mt_, p_nstate_, &move_, depth_ + 1) != -2){ ++valid_moves_; }
    }
    
    int valid_moves(){ return valid_moves_; }
};

// 駒を打ってみるためのクラス
class ActionTryDrop{
private:
    MoveTree::ptr mt_;
    osl::Move move_;
    osl::state::NumEffectState * p_nstate_;
    int depth_, max_depth_;
    int valid_moves_;
    
public:
    ActionTryDrop(MoveTree::ptr mt, osl::state::NumEffectState & nstate, int depth, int max_depth) : mt_(mt), p_nstate_(&nstate), depth_(depth), max_depth_(max_depth), valid_moves_(0) {}
        
    template<osl::Player pl> void doAction(const osl::Piece & pc, const osl::Square & sq){
        if(!(sq.isOnBoard()))return;
        
        move_ = osl::Move(sq, pc.ptype(), osl::BLACK);
        if(recursive_search(max_depth_, mt_, p_nstate_, &move_, depth_ + 1) != -2){ ++valid_moves_; }
    }
    
    int valid_moves(){ return valid_moves_; }
};

// 論理XOR
inline int logical_xor(int x, int y){
    return((x && !y) || (!x && y));
}

// 再帰的に探索。詰んだら1、詰まなかったら-1、無効な手なら-2、未確定なら0を返す
// p_moveには、ルール上動かせない手や、王手ではない手も含まれていてよい。
// 
// 補足：
// 局面を表すインスタンス（NumEffectStateクラス）をコピーする必要があるときは
// すべてこの関数の中で行っている。
int recursive_search(int max_depth, MoveTree::ptr mt, const osl::state::NumEffectState * p_nstate_orig, osl::Move * p_move = NULL, int depth = 1){
    if(depth > max_depth){
        mt->children.push_back(MoveTree::createNewPtr(MoveTree::over_limit));
        return 0;
    }
    
    MoveTree::ptr mt_new;
    int valid_moves = 0;
    
    // 元の盤面をコピー
    // MEMO: 単にNumEffectStateをコピーコンストラクタでコピーしたらセグフォった
    // TODO: こういう処理はAPIが提供されてそうなので調べる
    osl::Piece p;
    
    osl::state::SimpleState sstate;
    sstate.setTurn(p_nstate_orig->turn());
    for(int i = 0; i < osl::Piece::SIZE; ++i){
        p = p_nstate_orig->pieceOf(i);
        sstate.setPiece(p.owner(), p.square(), p.ptype());
    }
    sstate.initPawnMask(); // setPiece/setPieceAllが完了したあとに必要
    osl::state::NumEffectState nstate = osl::state::NumEffectState(sstate);
    
    // p_moveがNULLでないならば
    if(p_move){
        assert(logical_xor(depth % 2 == 1, p_move->player() == osl::BLACK));
        
        // 動かしてみる
        if(nstate.isValidMove(*p_move)){
            // 動かせる
            nstate.makeMove(*p_move);
            
            // （先手が指した結果の場合）後手玉が王手になっている
            // （後手が指した結果の場合）後手玉が王手になっていない
            if(logical_xor(depth % 2 == 1, nstate.inCheck(osl::WHITE))){
                mt_new = MoveTree::createNewPtr(*p_move);
                mt->children.push_back(mt_new);
            }else{
                return -2;
            }
        }else{
            // 動かせない
            return -2;
        }
    }else{
        mt_new = mt;
    }
    
    if(depth % 2 == 1){
        // 先手番 ここから
        
        // [駒を動かして王手]
        // 盤上にある自分の駒すべてについて、駒を動かせる場所を列挙する。
        // 駒を飛び越えるような場所や、盤の外が格納される場合もある。
        // それらは後でふるい落とす。
        ActionTryMove acm(mt_new, nstate, depth, max_depth);
        
        for(int i = 0; i < osl::Piece::SIZE; ++i){
            p = nstate.pieceOf(i);
            if(p.isOnBoardByOwner(osl::BLACK)){
                // 盤上にある先手の駒の場合
                nstate.forEachEffectOfPiece<ActionTryMove>(p, acm);
            }
        }
        valid_moves += acm.valid_moves();
        
        // [駒を打って王手]
        ActionTryDrop acd(mt_new, nstate, depth, max_depth);
        
        for(osl::Ptype pt = osl::PTYPE_BASIC_MIN; static_cast<int>(pt) <= static_cast<int>(osl::PTYPE_MAX); pt = static_cast<osl::Ptype>(static_cast<int>(pt) + 1)){
            if(pt == osl::KING) continue;
            
            if(nstate.countPiecesOnStand(osl::BLACK, pt) > 0){
                // もしptの駒を持っているなら
                osl::state::SimpleState sstate_tmp;
                sstate_tmp.setPiece(osl::WHITE, nstate.kingSquare(osl::WHITE), pt);
                sstate_tmp.initPawnMask();
                osl::state::NumEffectState nstate_tmp(sstate_tmp);
                
                nstate_tmp.forEachEffectOfPiece<ActionTryDrop>(nstate_tmp.pieceAt(nstate.kingSquare(osl::WHITE)), acd);
            }
        }
        valid_moves += acd.valid_moves();
        
        if(valid_moves == 0){
            // 先手が王手をかける手段がない場合（詰まない）
            mt_new->children.push_back(MoveTree::createNewPtr(MoveTree::cannot_checkmate));
            return -1;
        }else{
            return 0;
        }
        // 先手番 ここまで
    }else{
        // 後手番 ここから
        
        // [駒を動かして王手を回避]
        ActionTryMove acm(mt_new, nstate, depth, max_depth);
        
        for(int i = 0; i < osl::Piece::SIZE; ++i){
            p = nstate.pieceOf(i);
            if(p.isOnBoardByOwner(osl::WHITE)){
                // 盤上にある先手の駒の場合
                nstate.forEachEffectOfPiece<ActionTryMove>(p, acm);
            }
        }
        valid_moves += acm.valid_moves();
        
        // [駒を打って王手を回避]
        osl::Piece checking_piece;
        assert(nstate.findCheckPiece<osl::WHITE>(checking_piece));
        
        if(checking_piece != osl::Piece::EMPTY()){
            // ↑両王手ではないことを確認
            
            int diff_x = checking_piece.square().x() - nstate.kingSquare(osl::WHITE).x();
            int diff_y = checking_piece.square().y() - nstate.kingSquare(osl::WHITE).y();
            
            if(std::abs(diff_x) != 1 && std::abs(diff_y) != 1){
                // 王手を合駒打ちで防げる可能性がある場合
                // [補足]
                // 両王手ではない場合において、王手を合駒打ちで防げないのは、
                // ・王将の隣接マスの駒が王手をかけている場合
                // ・桂馬が王手をかけている場合
                // に限られる。これらはいずれの場合であっても、
                // 王手をかけている駒と王将とのxの差かyの差のどちらかが1になる。
                int dir_x = sign(diff_x);
                int dir_y = sign(diff_y);
                for(int i = nstate.kingSquare(osl::WHITE).x() + dir_x, j = nstate.kingSquare(osl::WHITE).y() + dir_y; i != checking_piece.square().x(); i += dir_x, j += dir_y){
                    for(osl::Ptype pt = osl::PTYPE_BASIC_MIN; static_cast<int>(pt) <= static_cast<int>(osl::PTYPE_MAX); pt = static_cast<osl::Ptype>(static_cast<int>(pt) + 1)){
                        if(pt == osl::KING) continue;

                        if(nstate.countPiecesOnStand(osl::WHITE, pt) > 0){
                            osl::Move move(osl::Square(i, j), pt, osl::WHITE);
                            if(recursive_search(max_depth, mt_new, &nstate, &move, depth + 1) != -2){ ++valid_moves; }
                        }
                    }
                }
            }
        }
        
        if(valid_moves == 0){
            // 後手が王手を回避する手段がない場合（詰み）
            if(p_move->isDrop() && p_move->ptype() == osl::PAWN){
                // 打ち歩詰め
                return -1;
            }else{
                mt_new->children.push_back(MoveTree::createNewPtr(MoveTree::checkmate));
                return 1;
            }
        }else{
            return 0;
        }
        // 後手番 ここまで
    }
}

void indent(int size){
    for(int i = 0; i < size; ++i){ std::cout << ' '; }
}

void print_tree(MoveTree::ptr mt, int depth = 0){
    for(std::list<MoveTree::ptr>::iterator it = mt->children.begin(); it != mt->children.end(); ++it){
        switch((*it)->status()){
        case MoveTree::simple_move:
            indent(depth * 2);
            std::cout << (depth + 1) << "." << *((*it)->move()) << std::endl;
            break;
        case MoveTree::checkmate:
            indent(depth * 2);
            std::cout << "<Checkmate>" << std::endl;
            break;
        case MoveTree::cannot_checkmate:
            indent(depth * 2);
            std::cout << "<Cannot checkmate>" << std::endl;
            break;
        case MoveTree::over_limit:
            std::cout << "<Over move limit>";
            break;
        default:
            std::cout << "<Unexpected result>";
            break;
        }
        print_tree(*it, depth + 1);
    }
}

// 探索した手順のうち、詰め手順となっていないものを削除する
// 方針としては、
// ・攻方が手番の局面においては、その先のどれか一つでも詰む手順であれば詰む
// ・受方が手番の局面においては、その先のどれか一つでも詰まない手順であれば詰まない
// 
// 返り値は
// trueなら詰む手順
// falseなら詰まない手順（探索手数上限にひっかかった場合含む）
bool prune_escaped_moves(MoveTree::ptr mt, int depth = 0){
    switch(mt->status()){
    case MoveTree::checkmate:
        return true;
    case MoveTree::cannot_checkmate: case MoveTree::over_limit:
        return false;
    default:
        // go to next
        break;
    }
    
    bool result;
    
    std::list<MoveTree::ptr>::iterator it = mt->children.begin();
    while(it != mt->children.end()){
        result = prune_escaped_moves(*it, depth + 1);
        if(!result){
            // イテレート中のSTLのlistから要素を安全に削除する方法
            // http://marupeke296.com/TIPS_No12_ListElementErase.html
            it = mt->children.erase(it);
            if(depth % 2 == 1) return false;
            continue;
        }
        ++it;
    }
    
    return(!(mt->children.empty()));
}

void set_problem(osl::state::SimpleState & sstate, int id){
    using namespace osl;
    
    switch(id){
    case 1:
        // 15手詰め（たぶん今のプログラムじゃ解くのが大変）
        sstate.setPiece(BLACK, Square(3, 3), PPAWN);
        sstate.setPiece(WHITE, Square(3, 1), LANCE);
        sstate.setPiece(WHITE, Square(2, 1), KING);
        sstate.setPiece(WHITE, Square(2, 6), PPAWN);
        sstate.setPiece(BLACK, Square::STAND(), LANCE); // ↓持ち駒
        sstate.setPiece(BLACK, Square::STAND(), PAWN);
        sstate.setPiece(BLACK, Square::STAND(), PAWN);
        sstate.setPiece(BLACK, Square::STAND(), PAWN);
        sstate.setPiece(BLACK, Square::STAND(), PAWN);
        sstate.setPieceAll(WHITE);
        break;
    case 2:
        // 3手詰め（簡単）
        sstate.setPiece(BLACK, Square(2, 3), PPAWN);
        sstate.setPiece(BLACK, Square(3, 3), PAWN);
        sstate.setPiece(WHITE, Square(2, 1), KING);
        sstate.setPieceAll(WHITE);
        break;
    case 3:
        // 3手詰め（簡単）
        sstate.setPiece(BLACK, Square(3, 3), ROOK);
        sstate.setPiece(WHITE, Square(1, 1), LANCE);
        sstate.setPiece(WHITE, Square(1, 2), KING);
        sstate.setPiece(WHITE, Square(1, 3), PAWN);
        sstate.setPiece(WHITE, Square(2, 1), KNIGHT);
        sstate.setPiece(WHITE, Square(3, 2), SILVER);
        sstate.setPieceAll(WHITE);
        break;
    case 4:
        // 5手詰め。打ち歩詰めが絡む
        sstate.setPiece(BLACK, Square(3, 3), GOLD);
        sstate.setPiece(BLACK, Square(2, 3), SILVER);
        sstate.setPiece(BLACK, Square(2, 5), KNIGHT);
        sstate.setPiece(WHITE, Square(1, 1), KING);
        sstate.setPiece(WHITE, Square(2, 1), LANCE);
        sstate.setPiece(WHITE, Square(3, 1), LANCE);
        sstate.setPiece(BLACK, Square::STAND(), PAWN); // 持ち駒
        sstate.setPieceAll(WHITE);
        break;
    case 5:
        // 1手詰め。突き歩詰め
        sstate.setPiece(BLACK, Square(3, 3), GOLD);
        sstate.setPiece(BLACK, Square(2, 3), SILVER);
        sstate.setPiece(BLACK, Square(2, 5), KNIGHT);
        sstate.setPiece(BLACK, Square(1, 3), PAWN);
        sstate.setPiece(WHITE, Square(1, 1), KING);
        sstate.setPiece(WHITE, Square(2, 1), LANCE);
        sstate.setPiece(WHITE, Square(3, 1), LANCE);
        sstate.setPiece(BLACK, Square::STAND(), PAWN); // 持ち駒
        sstate.setPieceAll(WHITE);
        break;
    case 6:
    default:
        // 古い詰将棋。5手詰だけど持ち駒が金銀2枚なので時間がかかるかも
        sstate.setPiece(BLACK, Square(3, 5), KNIGHT);
        sstate.setPiece(WHITE, Square(2, 2), KING);
        sstate.setPiece(WHITE, Square(1, 1), LANCE);
        sstate.setPiece(WHITE, Square(2, 1), KNIGHT);
        sstate.setPiece(WHITE, Square(1, 4), PAWN);
        sstate.setPiece(WHITE, Square(2, 4), PAWN);
        sstate.setPiece(BLACK, Square::STAND(), GOLD); // 持ち駒
        sstate.setPiece(BLACK, Square::STAND(), SILVER); // 持ち駒
        sstate.setPieceAll(WHITE);
        break;
    }
    sstate.initPawnMask(); // setPiece/setPieceAllが完了したあとに必要
}

void help_and_exit(const char * progname){
    std::cerr << "Usage: " << progname << " ProblemID MaxMoves [Display(1/2/3)]" << std::endl;
    std::cerr << "`Display(1/2/3)' means - 1: only problem, 2: problem and answer (default), 3: problem, process and answer." << std::endl;
    std::exit(1);
}

int main(int argc, char *argv[]){
    using namespace osl;
    int display = 2;
    int id;
    int max_moves;
    
    if(argc <= 2){
        help_and_exit(argv[0]);
    }else{
        id = std::atoi(argv[1]);
        max_moves = std::atoi(argv[2]);
        if(argc >= 4){
            display = atoi(argv[3]);
            if(display < 1 || display > 3) help_and_exit(argv[0]);
        }
    }
    
    // 盤面を定義
    state::SimpleState sstate;
    
    set_problem(sstate, id);
    
    state::NumEffectState nstate(sstate);
    std::cout << nstate << std::endl;
    
    if(display >= 2){
        // 詰み手順を探索
        MoveTree::ptr mt_init(new MoveTree());
        recursive_search(max_moves, mt_init, &nstate);
    
        // 探索した手順を表示
        if(display >= 3){
            std::cout << "---------- FullResult ----------" << std::endl;
            print_tree(mt_init);
        }
        
        // 探索した手順のうち、詰め手順として有効なもののみ残した上で表示する
        prune_escaped_moves(mt_init);
        std::cout << "---------- Answer ----------" << std::endl;
        print_tree(mt_init);
    }
    
    return 0;
}

