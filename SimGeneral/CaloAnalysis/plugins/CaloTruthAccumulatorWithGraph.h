#ifndef CaloAnalysis_CaloTruthAccumulatorWithGraph_h
#define CaloAnalysis_CaloTruthAccumulatorWithGraph_h

#include "SimGeneral/MixingModule/interface/DigiAccumulatorMixMod.h"
// BOOST GRAPH LIBRARY
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/breadth_first_search.hpp>
#include <boost/graph/depth_first_search.hpp>

#include <memory>  // required for std::auto_ptr
#include <unordered_map>
#include "SimDataFormats/CaloAnalysis/interface/CaloParticleFwd.h"
#include "SimDataFormats/CaloAnalysis/interface/SimClusterFwd.h"
#include "SimDataFormats/GeneratorProducts/interface/HepMCProduct.h"

#include "DataFormats/ForwardDetId/interface/HGCalDetId.h"
#include "FWCore/Framework/interface/ESHandle.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "Geometry/HGCalGeometry/interface/HGCalGeometry.h"
#include "Geometry/HcalTowerAlgo/interface/HcalGeometry.h"

typedef unsigned Index_t;
typedef int Barcode_t;
typedef std::pair<Index_t, Index_t> IndexPair_t;
typedef std::pair<IndexPair_t, float> SimHitInfo_t;
typedef std::pair<Barcode_t, Index_t> BarcodeIndexPair_t;
typedef std::pair<Barcode_t, Barcode_t> BarcodePair_t;

// Forward declarations
namespace edm {
class ParameterSet;
class ConsumesCollector;
namespace stream {
class EDProducerBase;
}
class Event;
class EventSetup;
class StreamID;
}
class PileUpEventPrincipal;
class PCaloHit;
class SimTrack;
class SimVertex;

using namespace boost;

/* GRAPH DEFINITIONS

   The graph represents the full decay chain.

   The parent-child relationship is the natural one, following "time".

   Each edge has a property (edge_weight_t) that holds a const pointer to the
   SimTrack that connects the 2 vertices of the edge, the number of simHits
   associated to that simTrack and the cumulative number of simHits of itself
   and of all its children. Only simHits within the selected detectors are
   taken into account.

   Each vertex has a property (vertex_name_t) that holds a const pointer to the
   SimTrack that originated that vertex and the cumulative number of simHits of
   all its outgoing edges. The cumulative property is filled during the dfs
   exploration of the graph: if not explored the number is 0.

   Stable particles are recovered/added in a second iterations and are linked
   to ghost vertices with an offset starting from the highest generated vertex.
*/
struct EdgeProperty {
  EdgeProperty(const SimTrack* t, int h, int c)
      : simTrack(t), simHits(h), cumulative_simHits(c) {}
  const SimTrack* simTrack;
  int simHits;
  int cumulative_simHits;
};

struct VertexProperty {
  VertexProperty() : simTrack(nullptr), cumulative_simHits(0) {}
  VertexProperty(const SimTrack* t, int c) : simTrack(t), cumulative_simHits(c) {}
  VertexProperty(const VertexProperty& other)
      : simTrack(other.simTrack), cumulative_simHits(other.cumulative_simHits) {}
  const SimTrack* simTrack;
  int cumulative_simHits;
};

typedef property<edge_weight_t, EdgeProperty> EdgeParticleClustersProperty;
typedef property<vertex_name_t, VertexProperty> VertexMotherParticleProperty;
typedef adjacency_list<listS, vecS, directedS, VertexMotherParticleProperty,
                       EdgeParticleClustersProperty>
    DecayChain;

class CaloTruthAccumulatorWithGraph : public DigiAccumulatorMixMod {
 public:
  explicit CaloTruthAccumulatorWithGraph(const edm::ParameterSet& config,
                                         edm::stream::EDProducerBase& mixMod,
                                         edm::ConsumesCollector& iC);

 private:
  void initializeEvent(const edm::Event& event, const edm::EventSetup& setup) override;
  void accumulate(const edm::Event& event, const edm::EventSetup& setup) override;
  void accumulate(const PileUpEventPrincipal& event, const edm::EventSetup& setup,
                  edm::StreamID const&) override;
  void finalizeEvent(edm::Event& event, const edm::EventSetup& setup) override;
  void beginLuminosityBlock(edm::LuminosityBlock const& lumi,
                            edm::EventSetup const& setup) override;

  /** @brief Both forms of accumulate() delegate to this templated method. */
  template <class T>
  void accumulateEvent(const T& event, const edm::EventSetup& setup,
                       const edm::Handle<edm::HepMCProduct>& hepMCproduct);

  /** @brief Fills the supplied vector with pointers to the SimHits, checking for bad modules if
   * required */
  template <class T>
  void fillSimHits(std::vector<std::pair<DetId, const PCaloHit*> >& returnValue,
                   std::map<int, std::map<int, float> >& simTrackDetIdEnergyMap, const T& event,
                   const edm::EventSetup& setup);

  const std::string
      messageCategory_;  ///< The message category used to send messages to MessageLogger

  struct calo_particles {
    std::vector<uint32_t> sc_start_;
    std::vector<uint32_t> sc_stop_;

    void swap(calo_particles& oth) {
      sc_start_.swap(oth.sc_start_);
      sc_stop_.swap(oth.sc_stop_);
    }

    void clear() {
      sc_start_.clear();
      sc_stop_.clear();
    }
  };

  calo_particles m_caloParticles;
  double caloStartZ;

  std::unordered_map<Index_t, float> m_detIdToTotalSimEnergy;  // keep track of cell normalizations
  std::unordered_multimap<Barcode_t, Index_t> m_simHitBarcodeToIndex;

  /** The maximum bunch crossing BEFORE the signal crossing to create TrackinParticles for. Use
   * positive values. If set to zero no
   * previous bunches are added and only in-time, signal and after bunches (defined by
   * maximumSubsequentBunchCrossing_) are used.*/
  const unsigned int maximumPreviousBunchCrossing_;
  /** The maximum bunch crossing AFTER the signal crossing to create TrackinParticles for. E.g. if
   * set to zero only
   * uses the signal and in time pileup (and previous bunches defined by the
   * maximumPreviousBunchCrossing_ parameter). */
  const unsigned int maximumSubsequentBunchCrossing_;

  const edm::InputTag simTrackLabel_;
  const edm::InputTag simVertexLabel_;
  edm::Handle<std::vector<SimTrack> > hSimTracks;
  edm::Handle<std::vector<SimVertex> > hSimVertices;

  std::vector<edm::InputTag> collectionTags_;
  edm::InputTag genParticleLabel_;
  /// Needed to add HepMC::GenVertex to SimVertex
  edm::InputTag hepMCproductLabel_;

  const double minEnergy_, maxPseudoRapidity_;

  bool selectorFlag_;
  /// Uses the same config as selector_, but can be used to drop out early since selector_ requires
  /// the TrackingParticle to be created first.
  bool chargedOnly_;
  /// Uses the same config as selector_, but can be used to drop out early since selector_ requires
  /// the TrackingParticle to be created first.
  bool signalOnly_;

  bool barcodeLogicWarningAlready_;

  /** @brief When counting hits, allows hits in different detectors to have a different process
   * type.
   *
   * Fast sim PCaloHits seem to have a peculiarity where the process type (as reported by
   * PCaloHit::processType()) is
   * different for the tracker than the muons. When counting how many hits there are, the code
   * usually only counts
   * the number of hits that have the same process type as the first hit. Setting this to true will
   * also count hits
   * that have the same process type as the first hit in the second detector.<br/>
   */
  //	bool allowDifferentProcessTypeForDifferentDetectors_;
 public:
  // These always go hand in hand, and I need to pass them around in the internal
  // functions, so I might as well package them up in a struct.
  struct OutputCollections {
    std::unique_ptr<SimClusterCollection> pSimClusters;
    std::unique_ptr<CaloParticleCollection> pCaloParticles;
    //		std::auto_ptr<TrackingVertexCollection> pTrackingVertices;
    //		TrackingParticleRefProd refTrackingParticles;
    //		TrackingVertexRefProd refTrackingVertexes;
  };

 private:
  const HGCalTopology* hgtopo_[2];
  const HGCalDDDConstants* hgddd_[2];
  const HcalDDDRecConstants* hcddd_;
  OutputCollections output_;
};

#endif  // end of "#ifndef CaloAnalysis_CaloTruthAccumulatorWithGraph_h"
