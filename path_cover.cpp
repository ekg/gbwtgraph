#include <gbwtgraph/path_cover.h>

#include <algorithm>
#include <cassert>
#include <deque>
#include <limits>
#include <map>
#include <stack>

namespace gbwtgraph
{

//------------------------------------------------------------------------------

std::vector<std::vector<nid_t>>
weakly_connected_components(const HandleGraph& graph)
{
  nid_t min_id = graph.min_node_id(), max_id = graph.max_node_id();

  std::vector<std::vector<nid_t>> components;
  sdsl::bit_vector found(max_id + 1 - min_id, 0);
  graph.for_each_handle([&](const handle_t& handle)
  {
    nid_t start_id = graph.get_id(handle);
    if(found[start_id - min_id]) { return; }
    components.emplace_back();
    std::stack<handle_t> handles;
    handles.push(handle);
    while(!(handles.empty()))
    {
      handle_t h = handles.top(); handles.pop();
      nid_t id = graph.get_id(h);
      if(found[id - min_id]) { continue; }
      found[id - min_id] = true;
      components.back().push_back(id);
      auto push = [&handles](const handle_t& next) { handles.push(next); };
      graph.follow_edges(h, false, push);
      graph.follow_edges(h, true, push);
    }
  }, false);

  return components;
}

std::vector<handle_t>
reverse_complement(const HandleGraph& graph, std::vector<handle_t>& forward)
{
  std::vector<handle_t> result = forward;
  std::reverse(result.begin(), result.end());
  for(handle_t& handle : result) { handle = graph.flip(handle); }
  return result;
}

std::vector<handle_t>
forward_window(const HandleGraph& graph, const std::deque<handle_t>& path, const handle_t& successor, size_t k)
{
  std::vector<handle_t> forward;
  forward.reserve(k);
  forward.insert(forward.end(), path.end() - (k - 1), path.end());
  forward.push_back(successor);

  std::vector<handle_t> reverse = reverse_complement(graph, forward);
  return (forward < reverse ? forward : reverse);
}

std::vector<handle_t>
backward_window(const HandleGraph& graph, const std::deque<handle_t>& path, const handle_t& predecessor, size_t k)
{
  std::vector<handle_t> forward;
  forward.reserve(k);
  forward.push_back(predecessor);
  forward.insert(forward.end(), path.begin(), path.begin() + (k - 1));

  std::vector<handle_t> reverse = reverse_complement(graph, forward);
  return (forward < reverse ? forward : reverse);
}

//------------------------------------------------------------------------------

/*
  The best candidate is the one with the lowest coverage so far.
*/

struct SimpleCoverage
{
  typedef size_t coverage_t;
  typedef std::pair<nid_t, coverage_t> node_coverage_t;

  static std::vector<node_coverage_t>::iterator find_first(std::vector<node_coverage_t>& array, nid_t id)
  {
    auto iter = std::lower_bound(array.begin(), array.end(), node_coverage_t(id, no_coverage()));
    assert(iter != array.end());
    assert(iter->first == id);
    return iter;
  }

  static void increase_coverage(coverage_t& coverage)
  {
    coverage++;
  }

  static void increase_coverage(node_coverage_t& node)
  {
    node.second++;
  }

  static coverage_t no_coverage() { return 0; }
  static coverage_t worst_coverage() { return std::numeric_limits<coverage_t>::max(); }

  // Should a be given priority over b?
  static bool give_priority(const coverage_t& a, const coverage_t& b)
  {
    return (a < b);
  }

  // Should a be given priority over b?
  static bool give_priority(const node_coverage_t& a, const node_coverage_t& b)
  {
    return (a.second < b.second);
  }
};

//------------------------------------------------------------------------------

template<class Coverage>
gbwt::GBWT
generic_path_cover(const HandleGraph& graph, size_t n, size_t k, gbwt::size_type batch_size, gbwt::size_type sample_interval, bool show_progress)
{
  typedef typename Coverage::coverage_t coverage_t;
  typedef typename Coverage::node_coverage_t node_coverage_t;

  // Sanity checks.
  size_t node_count = graph.get_node_count();
  if(node_count == 0 || n == 0) { return gbwt::GBWT(); }
  if(k < PATH_COVER_MIN_K)
  {
    std::cerr << "path_cover_gbwt(): Window length (" << k << ") must be at least " << PATH_COVER_MIN_K << std::endl;
    return gbwt::GBWT();
  }
  nid_t min_id = graph.min_node_id();
  if(min_id < 1)
  {
    std::cerr << "path_cover_gbwt(): Minimum node id (" << min_id << ") must be positive" << std::endl;
    return gbwt::GBWT();
  }
  nid_t max_id = graph.max_node_id();

  // Find weakly connected components, ignoring the directions of the edges.
  std::vector<std::vector<nid_t>> components = weakly_connected_components(graph);

  // GBWT construction parameters. Adjust the batch size down for small graphs.
  // We will also set basic metadata: n samples with each component as a separate contig.
  gbwt::Verbosity::set(gbwt::Verbosity::SILENT);
  gbwt::size_type node_width = gbwt::bit_length(gbwt::Node::encode(max_id, true));
  batch_size = std::min(batch_size, static_cast<gbwt::size_type>(2 * n * (node_count + components.size())));
  gbwt::GBWTBuilder builder(node_width, batch_size, sample_interval);
  builder.index.addMetadata();

  // Handle each component separately.
  for(size_t contig = 0; contig < components.size(); contig++)
  {
    if(show_progress)
    {
      std::cerr << "Processing component " << (contig + 1) << " / " << components.size() << std::endl;
    }
    std::vector<nid_t>& component = components[contig];
    std::vector<node_coverage_t> node_coverage;
    node_coverage.reserve(component.size());
    for(nid_t id : component) { node_coverage.emplace_back(id, Coverage::no_coverage()); }
    component = std::vector<nid_t>(); // Save a little bit of memory.
    std::map<std::vector<handle_t>, coverage_t> path_coverage; // Path and its reverse complement are equivalent.

    // Generate n paths in the component.
    for(size_t i = 0; i < n; i++)
    {
      // Choose a starting node with minimum coverage and then sort the nodes by id.
      std::deque<handle_t> path;
      std::sort(node_coverage.begin(), node_coverage.end(), [](const node_coverage_t& a, const node_coverage_t& b) -> bool
      {
        return Coverage::give_priority(a, b);
      });
      path.push_back(graph.get_handle(node_coverage.front().first, false));
      Coverage::increase_coverage(node_coverage.back());
      std::sort(node_coverage.begin(), node_coverage.end(), [](const node_coverage_t& a, const node_coverage_t& b) -> bool
      {
        return (a.first < b.first);
      });

      // Extend the path in both directions.
      // FIXME generalize to haplotypes
      // FIXME we need to initialize the coverages correctly
      bool forward_success = true, backward_success = true;
      while((forward_success || backward_success) && path.size() < node_coverage.size())
      {
        coverage_t best_coverage;
        handle_t best;
        auto update_best = [&best_coverage, &best](const coverage_t& coverage, const handle_t& candidate)
        {
          if(Coverage::give_priority(coverage, best_coverage))
          {
            best_coverage = coverage;
            best = candidate;
          }
        };

        // Extend forward.
        forward_success = false;
        best_coverage = Coverage::worst_coverage();
        graph.follow_edges(path.back(), false, [&](const handle_t& next)
        {
          forward_success = true;
          if(path.size() + 1 < k) // Node coverage.
          {
            auto iter = Coverage::find_first(node_coverage, graph.get_id(next));
            update_best(iter->second, next);
          }
          else
          {
            std::vector<handle_t> window = forward_window(graph, path, next, k);
            update_best(path_coverage[window], next);
          }
        });
        if(forward_success)
        {
          if(path.size() + 1 >= k)
          {
            std::vector<handle_t> window = forward_window(graph, path, best, k);
            Coverage::increase_coverage(path_coverage[window]);
          }
          auto iter = Coverage::find_first(node_coverage, graph.get_id(best));
          Coverage::increase_coverage(*iter);
          path.push_back(best);
          if(path.size() >= node_coverage.size()) { break; }
        }

        // Extend backward.
        backward_success = false;
        best_coverage = Coverage::worst_coverage();
        graph.follow_edges(path.front(), true, [&](const handle_t& prev)
        {
          backward_success = true;
          if(path.size() + 1 < k) // Node coverage.
          {
            auto iter = Coverage::find_first(node_coverage, graph.get_id(prev));
            update_best(iter->second, prev);
          }
          else
          {
            std::vector<handle_t> window = backward_window(graph, path, prev, k);
            update_best(path_coverage[window], prev);
          }
        });
        if(backward_success)
        {
          if(path.size() + 1 >= k)
          {
            std::vector<handle_t> window = backward_window(graph, path, best, k);
            Coverage::increase_coverage(path_coverage[window]);
          }
          auto iter = Coverage::find_first(node_coverage, graph.get_id(best));
          Coverage::increase_coverage(*iter);
          path.push_front(best);
        }
      }

      // Insert the path and its name into the index.
      gbwt::vector_type buffer;
      buffer.reserve(path.size());
      for(handle_t handle : path)
      {
        buffer.push_back(gbwt::Node::encode(graph.get_id(handle), graph.get_is_reverse(handle)));
      }
      builder.insert(buffer, true);
      builder.index.metadata.addPath(
      {
        static_cast<gbwt::PathName::path_name_type>(i),
        static_cast<gbwt::PathName::path_name_type>(contig),
        static_cast<gbwt::PathName::path_name_type>(0),
        static_cast<gbwt::PathName::path_name_type>(0)
      });
    }
  }

  // Finish the construction, add basic metadata, and return the GBWT.
  builder.finish();
  builder.index.metadata.setSamples(n);
  builder.index.metadata.setContigs(components.size());
  builder.index.metadata.setHaplotypes(n);
  if(show_progress)
  {
    gbwt::operator<<(std::cerr, builder.index.metadata) << std::endl;
  }
  return gbwt::GBWT(builder.index);
}

//------------------------------------------------------------------------------

gbwt::GBWT
path_cover_gbwt(const HandleGraph& graph, size_t n, size_t k, gbwt::size_type batch_size, gbwt::size_type sample_interval, bool show_progress)
{
  return generic_path_cover<SimpleCoverage>(graph, n, k, batch_size, sample_interval, show_progress);
}

/*gbwt::GBWT
path_cover_gbwt(const HandleGraph& graph, size_t n, size_t k, gbwt::size_type batch_size, gbwt::size_type sample_interval, bool show_progress)
{
  return generic_path_cover<LocalHaplotypes>(graph, n, k, batch_size, sample_interval, show_progress);
}*/

//------------------------------------------------------------------------------

} // namespace gbwtgraph
