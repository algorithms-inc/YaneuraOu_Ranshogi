// NNUE評価関数の入力特徴量HalfKPの定義

#include "../../../config.h"

#if defined(EVAL_NNUE)

#include "half_kp.h"
#include "index_list.h"

#if defined(KACHIKIRE_DEBUG_KSQ)
#include <cstdlib>
#include <iostream>
#include <sstream>
#endif

namespace Eval {

namespace NNUE {

namespace Features {

// 玉の位置とBonaPieceから特徴量のインデックスを求める
template <Side AssociatedKing>
inline IndexType HalfKP<AssociatedKing>::MakeIndex(Square sq_k, BonaPiece p) {
  return static_cast<IndexType>(fe_end) * static_cast<IndexType>(sq_k) + p;
}

// 駒の情報を取得する
template <Side AssociatedKing>
inline void HalfKP<AssociatedKing>::GetPieces(
    const Position& pos, Color perspective,
    BonaPiece** pieces, Square* sq_target_k) {
  *pieces = (perspective == BLACK) ?
      pos.eval_list()->piece_list_fb() :
      pos.eval_list()->piece_list_fw();
  const PieceNumber target = (AssociatedKing == Side::kFriend) ?
      static_cast<PieceNumber>(PIECE_NUMBER_KING + perspective) :
      static_cast<PieceNumber>(PIECE_NUMBER_KING + ~perspective);
  *sq_target_k = static_cast<Square>(((*pieces)[target] - f_king) % SQ_NB);

#if defined(KACHIKIRE_DEBUG_KSQ)
  // デバッグ用: 玉の位置が盤外になっていたら(駒リストの玉の欄が壊れている)、局面と駒リストを出して止める
  // Square(enum)のまま負数と比較すると、コンパイラが「常に偽」として消すことがあるのでintで比較する
  const int k_raw = (static_cast<int>((*pieces)[target]) - static_cast<int>(f_king)) % static_cast<int>(SQ_NB);
  if (k_raw < 0 || k_raw >= static_cast<int>(SQ_NB)) {
    std::ostringstream oss;
    oss << "info string BAD_KSQ perspective=" << (int)perspective
        << " king_slot=" << (int)(*pieces)[target]
        << " sq=" << (int)*sq_target_k
        << " sfen " << pos.sfen() << " list";
    for (int i = 0; i < EvalList::MAX_LENGTH; ++i)
      oss << ' ' << (int)(*pieces)[i];
    std::cout << oss.str() << std::endl;
    std::abort();
  }
#endif
}

// 特徴量のうち、値が1であるインデックスのリストを取得する
template <Side AssociatedKing>
void HalfKP<AssociatedKing>::AppendActiveIndices(
    const Position& pos, Color perspective, IndexList* active) {
  // コンパイラの警告を回避するため、配列サイズが小さい場合は何もしない
  if (RawFeatures::kMaxActiveDimensions < kMaxActiveDimensions) return;

  BonaPiece* pieces;
  Square sq_target_k;
  GetPieces(pos, perspective, &pieces, &sq_target_k);
  for (PieceNumber i = PIECE_NUMBER_ZERO; i < PIECE_NUMBER_KING; ++i) {
    active->push_back(MakeIndex(sq_target_k, pieces[i]));
  }
}

// 特徴量のうち、一手前から値が変化したインデックスのリストを取得する
template <Side AssociatedKing>
void HalfKP<AssociatedKing>::AppendChangedIndices(
    const Position& pos, Color perspective,
    IndexList* removed, IndexList* added) {
  BonaPiece* pieces;
  Square sq_target_k;
  GetPieces(pos, perspective, &pieces, &sq_target_k);
  const auto& dp = pos.state()->dirtyPiece;
  for (int i = 0; i < dp.dirty_num; ++i) {
    if (dp.pieceNo[i] >= PIECE_NUMBER_KING) continue;
    const auto old_p = static_cast<BonaPiece>(
        dp.changed_piece[i].old_piece.from[perspective]);
    removed->push_back(MakeIndex(sq_target_k, old_p));
    const auto new_p = static_cast<BonaPiece>(
        dp.changed_piece[i].new_piece.from[perspective]);
    added->push_back(MakeIndex(sq_target_k, new_p));
  }
}

template class HalfKP<Side::kFriend>;
template class HalfKP<Side::kEnemy>;

}  // namespace Features

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_NNUE)
