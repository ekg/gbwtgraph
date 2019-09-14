#include <gtest/gtest.h>

#include <map>
#include <set>
#include <vector>

#include <gbwt/utils.h>

#include <gbwtgraph/minimizer.h>

using namespace gbwtgraph;

namespace
{

//------------------------------------------------------------------------------

DefaultMinimizerIndex::minimizer_type
get_minimizer(DefaultMinimizerIndex::key_type key, DefaultMinimizerIndex::offset_type offset = 0, bool orientation = false)
{
  return { key, key.hash(), offset, orientation };
}

//------------------------------------------------------------------------------

TEST(ObjectManipulation, EmptyIndex)
{
  DefaultMinimizerIndex default_index;
  DefaultMinimizerIndex default_copy(default_index);
  DefaultMinimizerIndex alt_index(15, 6);
  DefaultMinimizerIndex alt_copy(alt_index);
  EXPECT_EQ(default_index, default_copy) << "A copy of the default index is not identical to the original";
  EXPECT_EQ(alt_index, alt_copy) << "A copy of a parametrized index is not identical to the original";
  EXPECT_NE(default_index, alt_index) << "Default and parametrized indexes are identical";
}

TEST(ObjectManipulation, Contents)
{
  DefaultMinimizerIndex default_index;
  DefaultMinimizerIndex default_copy(default_index);

  // Different contents.
  default_index.insert(get_minimizer(1), make_pos_t(1, false, 3));
  EXPECT_NE(default_index, default_copy) << "Empty index is identical to nonempty index";

  // Same key, different value.
  default_copy.insert(get_minimizer(1), make_pos_t(2, false, 3));
  EXPECT_NE(default_index, default_copy) << "Indexes with different values are identical";

  // Same contents.
  default_copy = default_index;
  EXPECT_EQ(default_index, default_copy) << "A copy of a nonempty index is not identical to the original";
}

TEST(ObjectManipulation, Swap)
{
  DefaultMinimizerIndex first, second;
  first.insert(get_minimizer(1), make_pos_t(1, false, 3));
  second.insert(get_minimizer(2), make_pos_t(2, false, 3));

  DefaultMinimizerIndex first_copy(first), second_copy(second);
  first.swap(second);
  EXPECT_NE(first, first_copy) << "Swapping did not change the first index";
  EXPECT_EQ(first, second_copy) << "The first index was not swapped correctly";
  EXPECT_EQ(second, first_copy) << "The second index was not swapped correctly";
  EXPECT_NE(second, second_copy) << "Swapping did not change the second index";
}

TEST(ObjectManipulation, Serialization)
{
  DefaultMinimizerIndex index(15, 6);
  index.insert(get_minimizer(1), make_pos_t(1, false, 3));
  index.insert(get_minimizer(2), make_pos_t(1, false, 3));
  index.insert(get_minimizer(2), make_pos_t(2, false, 3));

  std::string filename = gbwt::TempFile::getName("minimizer");
  std::ofstream out(filename, std::ios_base::binary);
  index.serialize(out);
  out.close();

  DefaultMinimizerIndex copy;
  std::ifstream in(filename, std::ios_base::binary);
  copy.deserialize(in);
  in.close();
  gbwt::TempFile::remove(filename);

  EXPECT_EQ(index, copy) << "Loaded index is not identical to the original";
}

//------------------------------------------------------------------------------

/*
  wang_hash_64() order of 3-mers:
  AAT < TGT < TTG < TAT < ATA < TCG < ATT < ACA < GAA < ACT < TAC < CGA < CAA < GTA < TTC < AGT
*/

class MinimizerExtraction : public ::testing::Test
{
public:
  std::string str, rev;

  MinimizerExtraction()
  {
  }

  void SetUp() override
  {
    this->str = "CGAATACAATACT";
    this->rev = reverse_complement(this->str);
  }
};

TEST_F(MinimizerExtraction, LeftmostOccurrence)
{
  DefaultMinimizerIndex index(3, 2);
  DefaultMinimizerIndex::minimizer_type correct = get_minimizer(0 * 16 + 0 * 4 + 3 * 1, 2); // AAT
  DefaultMinimizerIndex::minimizer_type result = index.minimizer(this->str.begin(), this->str.end());
  EXPECT_EQ(result, correct) << "The leftmost minimizer was not found";
}

TEST_F(MinimizerExtraction, AllMinimizers)
{
  DefaultMinimizerIndex index(3, 2);
  std::vector<DefaultMinimizerIndex::minimizer_type> correct
  {
    get_minimizer(3 * 16 + 1 * 4 + 2 * 1, 2, true),   // TCG
    get_minimizer(0 * 16 + 0 * 4 + 3 * 1, 2, false),  // AAT
    get_minimizer(3 * 16 + 0 * 4 + 3 * 1, 5, true),   // TAT
    get_minimizer(3 * 16 + 2 * 4 + 3 * 1, 7, true),   // TGT
    get_minimizer(0 * 16 + 0 * 4 + 3 * 1, 7, false),  // AAT
    get_minimizer(3 * 16 + 0 * 4 + 3 * 1, 10, true),  // TAT
    get_minimizer(0 * 16 + 1 * 4 + 3 * 1, 10, false)  // ACT
  };
  std::vector<DefaultMinimizerIndex::minimizer_type> result = index.minimizers(this->str.begin(), this->str.end());
  EXPECT_EQ(result, correct) << "Did not find the correct minimizers";
}

TEST_F(MinimizerExtraction, WindowLength)
{
  DefaultMinimizerIndex index(3, 3);
  std::vector<DefaultMinimizerIndex::minimizer_type> correct
  {
    get_minimizer(0 * 16 + 0 * 4 + 3 * 1, 2, false),  // AAT
    get_minimizer(3 * 16 + 2 * 4 + 3 * 1, 7, true),   // TGT
    get_minimizer(0 * 16 + 0 * 4 + 3 * 1, 7, false),  // AAT
    get_minimizer(3 * 16 + 0 * 4 + 3 * 1, 10, true)   // TAT
  };
  std::vector<DefaultMinimizerIndex::minimizer_type> result = index.minimizers(this->str.begin(), this->str.end());
  EXPECT_EQ(result, correct) << "Did not find the correct minimizers";
}

TEST_F(MinimizerExtraction, InvalidCharacters)
{
  std::string weird = "CGAATAxAATACT";
  DefaultMinimizerIndex index(3, 2);
  std::vector<DefaultMinimizerIndex::minimizer_type> correct
  {
    get_minimizer(3 * 16 + 1 * 4 + 2 * 1, 2, true),   // TCG
    get_minimizer(0 * 16 + 0 * 4 + 3 * 1, 2, false),  // AAT
    get_minimizer(3 * 16 + 0 * 4 + 3 * 1, 5, true),   // TAT
    get_minimizer(0 * 16 + 0 * 4 + 3 * 1, 7, false),  // AAT
    get_minimizer(3 * 16 + 0 * 4 + 3 * 1, 10, true),  // TAT
    get_minimizer(0 * 16 + 1 * 4 + 3 * 1, 10, false)  // ACT
  };
  std::vector<DefaultMinimizerIndex::minimizer_type> result = index.minimizers(weird.begin(), weird.end());
  EXPECT_EQ(result, correct) << "Did not find the correct minimizers";
}

TEST_F(MinimizerExtraction, BothOrientations)
{
  DefaultMinimizerIndex index(3, 2);
  std::vector<DefaultMinimizerIndex::minimizer_type> forward_minimizers = index.minimizers(this->str.begin(), this->str.end());
  std::vector<DefaultMinimizerIndex::minimizer_type> reverse_minimizers = index.minimizers(this->rev.begin(), this->rev.end());
  ASSERT_EQ(forward_minimizers.size(), reverse_minimizers.size()) << "Different number of minimizers in forward and reverse orientations";
  for(size_t i = 0; i < forward_minimizers.size(); i++)
  {
    DefaultMinimizerIndex::minimizer_type& f = forward_minimizers[i];
    DefaultMinimizerIndex::minimizer_type& r = reverse_minimizers[forward_minimizers.size() - 1 - i];
    EXPECT_EQ(f.key, r.key) << "Wrong key for minimizer " << i;
    EXPECT_EQ(f.offset, this->str.length() - 1 - r.offset) << "Wrong offset for minimizer " << i;
    EXPECT_NE(f.is_reverse, r.is_reverse) << "Wrong orientation for minimizer " << i;
  }
}

//------------------------------------------------------------------------------

class CorrectKmers : public ::testing::Test
{
public:
  typedef std::map<DefaultMinimizerIndex::key_type, std::set<pos_t>> result_type;

  size_t total_keys;

  CorrectKmers()
  {
  }

  void SetUp() override
  {
    this->total_keys = 16;
  }

  void check_minimizer_index(const DefaultMinimizerIndex& index,
                             const result_type& correct_values,
                             size_t keys, size_t values, size_t unique)
  {
    ASSERT_EQ(index.size(), keys) << "Wrong number of keys";
    ASSERT_EQ(index.values(), values) << "Wrong number of values";
    EXPECT_EQ(index.unique_keys(), unique) << "Wrong number of unique keys";

    for(auto iter = correct_values.begin(); iter != correct_values.end(); ++iter)
    {
      std::vector<pos_t> result = index.find(get_minimizer(iter->first));
      std::vector<pos_t> correct(iter->second.begin(), iter->second.end());
      EXPECT_EQ(result, correct) << "Wrong positions for key " << iter->first;
    }
  }
};

TEST_F(CorrectKmers, UniqueKeys)
{
  DefaultMinimizerIndex index;
  size_t keys = 0, values = 0, unique = 0;
  result_type correct_values;

  for(size_t i = 1; i <= this->total_keys; i++)
  {
    pos_t pos = make_pos_t(i, i & 1, i & DefaultMinimizerIndex::OFF_MASK);
    index.insert(get_minimizer(i), pos);
    correct_values[i].insert(pos);
    keys++; values++; unique++;
  }
  this->check_minimizer_index(index, correct_values, keys, values, unique);
}

TEST_F(CorrectKmers, MissingKeys)
{
  DefaultMinimizerIndex index;
  for(size_t i = 1; i <= this->total_keys; i++)
  {
    index.insert(get_minimizer(i), make_pos_t(i, i & 1, i & DefaultMinimizerIndex::OFF_MASK));
  }
  for(size_t i = this->total_keys + 1; i <= 2 * this->total_keys; i++)
  {
    EXPECT_TRUE(index.find(get_minimizer(i)).empty()) << "Nonempty value for key " << i;
  }
}

TEST_F(CorrectKmers, EmptyKeysValues)
{
  DefaultMinimizerIndex index;

  index.insert(get_minimizer(DefaultMinimizerIndex::key_type::no_key()), make_pos_t(1, false, 0));
  EXPECT_TRUE(index.find(get_minimizer(DefaultMinimizerIndex::key_type::no_key())).empty()) << "Nonempty value for empty key";

  index.insert(get_minimizer(this->total_keys + 1), DefaultMinimizerIndex::decode(DefaultMinimizerIndex::NO_VALUE));
  EXPECT_TRUE(index.find(get_minimizer(this->total_keys + 1)).empty()) << "Nonempty value after inserting empty value";
}

TEST_F(CorrectKmers, MultipleOccurrences)
{
  DefaultMinimizerIndex index;
  size_t keys = 0, values = 0, unique = 0;
  result_type correct_values;

  for(size_t i = 1; i <= this->total_keys; i++)
  {
    pos_t pos = make_pos_t(i, i & 1, i & DefaultMinimizerIndex::OFF_MASK);
    index.insert(get_minimizer(i), pos);
    correct_values[i].insert(pos);
    keys++; values++; unique++;
  }
  for(size_t i = 1; i <= this->total_keys; i += 2)
  {
    pos_t pos = make_pos_t(i + 1, i & 1, (i + 1) & DefaultMinimizerIndex::OFF_MASK);
    index.insert(get_minimizer(i), pos);
    correct_values[i].insert(pos);
    values++; unique--;
  }
  for(size_t i = 1; i <= this->total_keys; i += 4)
  {
    pos_t pos = make_pos_t(i + 2, i & 1, (i + 2) & DefaultMinimizerIndex::OFF_MASK);
    index.insert(get_minimizer(i), pos);
    correct_values[i].insert(pos);
    values++;
  }
  this->check_minimizer_index(index, correct_values, keys, values, unique);
}

TEST_F(CorrectKmers, DuplicateValues)
{
  DefaultMinimizerIndex index;
  size_t keys = 0, values = 0, unique = 0;
  result_type correct_values;

  for(size_t i = 1; i <= this->total_keys; i++)
  {
    pos_t pos = make_pos_t(i, i & 1, i & DefaultMinimizerIndex::OFF_MASK);
    index.insert(get_minimizer(i), pos);
    correct_values[i].insert(pos);
    keys++; values++; unique++;
  }
  for(size_t i = 1; i <= this->total_keys; i += 2)
  {
    pos_t pos = make_pos_t(i + 1, i & 1, (i + 1) & DefaultMinimizerIndex::OFF_MASK);
    index.insert(get_minimizer(i), pos);
    correct_values[i].insert(pos);
    values++; unique--;
  }
  for(size_t i = 1; i <= this->total_keys; i += 4)
  {
    pos_t pos = make_pos_t(i + 1, i & 1, (i + 1) & DefaultMinimizerIndex::OFF_MASK);
    index.insert(get_minimizer(i), pos);
  }
  this->check_minimizer_index(index, correct_values, keys, values, unique);
}

TEST_F(CorrectKmers, Rehashing)
{
  DefaultMinimizerIndex index;
  size_t keys = 0, values = 0, unique = 0;
  result_type correct_values;
  size_t threshold = index.max_keys();

  for(size_t i = 1; i <= threshold; i++)
  {
    pos_t pos = make_pos_t(i, i & 1, i & DefaultMinimizerIndex::OFF_MASK);
    index.insert(get_minimizer(i), pos);
    correct_values[i].insert(pos);
    keys++; values++; unique++;
  }
  ASSERT_EQ(index.max_keys(), threshold) << "Index capacity changed at threshold";

  {
    size_t i = threshold + 1;
    pos_t pos = make_pos_t(i, i & 1, i & DefaultMinimizerIndex::OFF_MASK);
    index.insert(get_minimizer(i), pos);
    correct_values[i].insert(pos);
    keys++; values++; unique++;
  }
  EXPECT_GT(index.max_keys(), threshold) << "Index capacity not increased after threshold";

  this->check_minimizer_index(index, correct_values, keys, values, unique);
}

//------------------------------------------------------------------------------

} // namespace
