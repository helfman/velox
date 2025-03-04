/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "velox/expression/tests/VectorFuzzer.h"
#include <codecvt>
#include <locale>
#include "velox/type/Timestamp.h"
#include "velox/vector/FlatVector.h"
#include "velox/vector/VectorTypeUtils.h"

namespace facebook::velox {

namespace {

// Generate random values for the different supported types.
template <typename T>
T rand(FuzzerGenerator&) {
  VELOX_NYI();
}

template <>
int8_t rand(FuzzerGenerator& rng) {
  return folly::Random::rand32(rng);
}

template <>
int16_t rand(FuzzerGenerator& rng) {
  return folly::Random::rand32(rng);
}

template <>
int32_t rand(FuzzerGenerator& rng) {
  return folly::Random::rand32(rng);
}

template <>
int64_t rand(FuzzerGenerator& rng) {
  return folly::Random::rand32(rng);
}

template <>
double rand(FuzzerGenerator& rng) {
  return folly::Random::randDouble01(rng);
}

template <>
float rand(FuzzerGenerator& rng) {
  return folly::Random::randDouble01(rng);
}

template <>
bool rand(FuzzerGenerator& rng) {
  return folly::Random::oneIn(2, rng);
}

template <>
Timestamp rand(FuzzerGenerator& rng) {
  return Timestamp(folly::Random::rand32(rng), folly::Random::rand32(rng));
}

template <>
uint32_t rand(FuzzerGenerator& rng) {
  return folly::Random::rand32(rng);
}

/// Unicode character ranges.
/// Source: https://jrgraphix.net/research/unicode_blocks.php
const std::map<UTF8CharList, std::vector<std::pair<char16_t, char16_t>>>
    kUTFChatSetMap{
        {UTF8CharList::ASCII,
         {
             /*Numbers*/ {'0', '9'},
             /*Upper*/ {'A', 'Z'},
             /*Lower*/ {'a', 'z'},
         }},
        {UTF8CharList::UNICODE_CASE_SENSITIVE,
         {
             /*Basic Latin*/ {u'\u0020', u'\u007F'},
             /*Cyrillic*/ {u'\u0400', u'\u04FF'},
         }},
        {UTF8CharList::EXTENDED_UNICODE,
         {
             /*Greek*/ {u'\u03F0', u'\u03FF'},
             /*Latin Extended A*/ {u'\u0100', u'\u017F'},
             /*Arabic*/ {u'\u0600', u'\u06FF'},
             /*Devanagari*/ {u'\u0900', u'\u097F'},
             /*Hebrew*/ {u'\u0600', u'\u06FF'},
             /*Hiragana*/ {u'\u3040', u'\u309F'},
             /*Punctuation*/ {u'\u2000', u'\u206F'},
             /*Sub/Super Script*/ {u'\u2070', u'\u209F'},
             /*Currency*/ {u'\u20A0', u'\u20CF'},
         }},
        {UTF8CharList::MATHEMATICAL_SYMBOLS,
         {
             /*Math Operators*/ {u'\u2200', u'\u22FF'},
             /*Number Forms*/ {u'\u2150', u'\u218F'},
             /*Geometric Shapes*/ {u'\u25A0', u'\u25FF'},
             /*Math Symbols*/ {u'\u27C0', u'\u27EF'},
             /*Supplemental*/ {u'\u2A00', u'\u2AFF'},
         }}};

FOLLY_ALWAYS_INLINE char16_t getRandomChar(
    FuzzerGenerator& rng,
    const std::vector<std::pair<char16_t, char16_t>>& charSet) {
  const auto& chars = charSet[rand<uint32_t>(rng) % charSet.size()];
  auto size = chars.second - chars.first;
  auto inc = (rand<uint32_t>(rng) % size);
  char16_t res = chars.first + inc;
  return res;
}

/// Generates a random string (string size and encoding are passed through
/// Options). Returns a StringView which uses `buf` as the underlying buffer.
StringView randString(
    FuzzerGenerator& rng,
    const VectorFuzzer::Options& opts,
    std::string& buf,
    std::wstring_convert<std::codecvt_utf8<char16_t>, char16_t>& converter) {
  buf.clear();
  std::u16string wbuf;
  const size_t stringLength = opts.stringVariableLength
      ? folly::Random::rand32(rng) % opts.stringLength
      : opts.stringLength;
  wbuf.resize(stringLength);

  for (size_t i = 0; i < stringLength; ++i) {
    auto encoding =
        opts.charEncodings[rand<uint32_t>(rng) % opts.charEncodings.size()];
    wbuf[i] = getRandomChar(rng, kUTFChatSetMap.at(encoding));
  }

  buf.append(converter.to_bytes(wbuf));
  return StringView(buf);
}

template <TypeKind kind>
variant randVariantImpl(
    FuzzerGenerator& rng,
    const VectorFuzzer::Options& opts) {
  using TCpp = typename TypeTraits<kind>::NativeType;
  if constexpr (std::is_same_v<TCpp, StringView>) {
    std::wstring_convert<std::codecvt_utf8<char16_t>, char16_t> converter;
    std::string buf;
    auto stringView = randString(rng, opts, buf, converter);

    if constexpr (kind == TypeKind::VARCHAR) {
      return variant(stringView);
    } else if constexpr (kind == TypeKind::VARBINARY) {
      return variant::binary(stringView);
    } else {
      VELOX_UNREACHABLE();
    }
  } else {
    return variant(rand<TCpp>(rng));
  }
}

template <TypeKind kind>
void fuzzFlatImpl(
    const VectorPtr& vector,
    FuzzerGenerator& rng,
    const VectorFuzzer::Options& opts) {
  using TFlat = typename KindToFlatVector<kind>::type;
  using TCpp = typename TypeTraits<kind>::NativeType;

  auto flatVector = vector->as<TFlat>();
  std::string strBuf;

  std::wstring_convert<std::codecvt_utf8<char16_t>, char16_t> converter;
  for (size_t i = 0; i < vector->size(); ++i) {
    if constexpr (std::is_same_v<TCpp, StringView>) {
      flatVector->set(i, randString(rng, opts, strBuf, converter));
    } else {
      flatVector->set(i, rand<TCpp>(rng));
    }
  }
}

} // namespace

VectorPtr VectorFuzzer::fuzz(const TypePtr& type) {
  VectorPtr vector;

  // One in 5 chance of adding a constant vector.
  if (oneIn(5)) {
    // One in 5 chance of adding a NULL constant vector.
    if (oneIn(5)) {
      vector = BaseVector::createNullConstant(type, opts_.vectorSize, pool_);
    } else {
      vector = BaseVector::createConstant(
          randVariant(type), opts_.vectorSize, pool_);
    }
  } else {
    vector = fuzzFlat(type);
  }

  // Toss a coin and add dictionary indirections.
  while (oneIn(2)) {
    vector = fuzzDictionary(vector);
  }
  return vector;
}

VectorPtr VectorFuzzer::fuzzFlat(const TypePtr& type) {
  auto vector = BaseVector::create(type, opts_.vectorSize, pool_);

  // First, fill it with random values.
  // TODO: We should bias towards edge cases (min, max, Nan, etc).
  auto kind = vector->typeKind();
  VELOX_DYNAMIC_SCALAR_TYPE_DISPATCH(fuzzFlatImpl, kind, vector, rng_, opts_);

  // Second, generate a random null vector.
  for (size_t i = 0; i < vector->size(); ++i) {
    if (oneIn(opts_.nullChance)) {
      vector->setNull(i, true);
    }
  }
  return vector;
}

VectorPtr VectorFuzzer::fuzzDictionary(const VectorPtr& vector) {
  const size_t vectorSize = vector->size();
  BufferPtr indices = AlignedBuffer::allocate<vector_size_t>(vectorSize, pool_);
  auto rawIndices = indices->asMutable<vector_size_t>();

  for (size_t i = 0; i < vectorSize; ++i) {
    rawIndices[i] = rand<vector_size_t>(rng_) % vectorSize;
  }

  // TODO: We can fuzz nulls here as well.
  return BaseVector::wrapInDictionary(
      BufferPtr(nullptr), indices, vectorSize, vector);
}

variant VectorFuzzer::randVariant(const TypePtr& arg) {
  return VELOX_DYNAMIC_SCALAR_TYPE_DISPATCH(
      randVariantImpl, arg->kind(), rng_, opts_);
}

} // namespace facebook::velox
