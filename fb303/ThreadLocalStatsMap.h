/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
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

#pragma once

#include <chrono>

#include <fb303/ThreadLocalStats.h>
#include <folly/Range.h>
#include <folly/ThreadLocal.h>
#include <folly/container/F14Map.h>
#include <folly/hash/Hash.h>
#include <folly/memory/SanitizeLeak.h>

namespace facebook::fb303 {

/**
 * ThreadLocalStatsMap subclasses ThreadLocalStats, and provides APIs for
 * updating statistics by name.
 *
 * This makes it fairly easy to use as a drop-in replacement for call sites
 * that were previously using the ServiceData APIs to update stats by name.
 * A ThreadLocalStatsMap object can be used instead of the fbData singleton,
 * and the stat update operations will become faster thread local operations in
 * order to reduce or eliminate lock contention.
 *
 * However, note that accessing statistics by name is less efficient than
 * defining your own TLTimeseries or TLHistogram counters and accessing them
 * directly, as it requires a string lookup on each update operation.  Where
 * possible, prefer to define your own ThreadLocalStats subclass so you can
 * directly access counter member variables rather than having to perform
 * string lookups on each update operation.
 */
template <class LockTraits>
class ThreadLocalStatsMapT : public ThreadLocalStatsT<LockTraits> {
 public:
  using TLCounter = TLCounterT<LockTraits>;
  using TLHistogram = TLHistogramT<LockTraits>;
  using TLTimeseries = TLTimeseriesT<LockTraits>;

  explicit ThreadLocalStatsMapT(ServiceData* serviceData = nullptr);

  /**
   * Add a value to the timeseries statistic with the specified name.
   *
   * Note that you must call ServiceData::addStatExportType() in order to
   * specify which statistics about this timeseries should be reported in the
   * fb303 counters.
   */
  void addStatValue(folly::StringPiece name, int64_t value = 1);
  void addStatValueAggregated(
      folly::StringPiece name,
      int64_t sum,
      int64_t numSamples);

  void
  addStatValue(folly::StringPiece name, int64_t value, ExportType exportType);
  void clearStat(folly::StringPiece name, ExportType exportType);

  /**
   * Add a value to the histogram with the specified name.
   *
   * Note that you must have first called ServiceData::addHistogram() in order
   * to specify the range and bucket width for this histogram.  If this
   * histogram has not been defined with addHistogram() yet, this call will
   * simply be ignored.
   */
  void addHistogramValue(folly::StringPiece name, int64_t value);

  /**
   * Increment a "regular-style" flat counter (no historical stats)
   */
  void incrementCounter(folly::StringPiece name, int64_t amount = 1);

  /*
   * Gets the TLTimeseries with the given name. Never returns NULL. The
   * TLTimeseries returned should not be shared with other threads.
   */
  std::shared_ptr<TLTimeseries> getTimeseriesSafe(folly::StringPiece name);
  std::shared_ptr<TLTimeseries> getTimeseriesSafe(
      folly::StringPiece name,
      size_t numBuckets,
      size_t numLevels,
      const ExportedStat::Duration levelDurations[]);
  /*
   * Clears the TLTimeseries with the given name for the current thread.
   */
  void clearTimeseriesSafe(folly::StringPiece name);

  /**
   * Gets the TLCounter with the given name. Never returns NULL. The
   * TLCounter returned should not be shared with other threads.
   */
  std::shared_ptr<TLCounter> getCounterSafe(folly::StringPiece name);

  /**
   * Gets the TLHistogram with the given name. Returns NULL if the global
   * histogram hasn't been created yet. The TLHistogram returned should not
   * be shared with other threads.
   */
  std::shared_ptr<TLHistogram> getHistogramSafe(folly::StringPiece name);

  void resetAllData();

 private:
  /*
   * This "lock" protects the named maps.  Since the maps should only ever be
   * accessed from a single thread, it doesn't provide real locking, but
   * instead only asserts that the accesses occur from the correct thread.
   *
   * If we used TLStatsThreadSafe::RegistryLock this would make the code truly
   * thread safe, so that any thread could update stats by name.  We could turn
   * this into a template parameter in the future, but for now no one needs the
   * fully thread-safe behavior.
   */
  using NamedMapLock = typename TLStatsNoLocking::RegistryLock;

  template <class StatType>
  class StatPtrBase {
   protected:
    static inline constexpr size_t nbits = sizeof(uintptr_t) * 8;
    static inline constexpr size_t ntypes = ExportTypeMeta::kNumExportTypes;
    static inline constexpr uintptr_t types_mask = ~uintptr_t(0)
        << (nbits - ntypes);

    static constexpr uintptr_t mask_(ExportType key) noexcept {
      assert(size_t(key) < ntypes);
      return uintptr_t(1) << (nbits - ntypes + size_t(key));
    }
  };

  /// Stores the export-type list alongside the stat-ptr.
  template <class StatType>
  class StatPtrFallback : private StatPtrBase<StatType> {
   private:
    std::shared_ptr<StatType> ptr_;
    mutable uintptr_t exports_{};

    using StatPtrBase<StatType>::mask_;

   public:
    explicit operator bool() const noexcept {
      return !!ptr_;
    }
    std::shared_ptr<StatType> ptr() const noexcept {
      return ptr_;
    }
    void ptr(std::shared_ptr<StatType>&& val) noexcept {
      ptr_ = std::move(val);
    }
    StatType* raw() const noexcept {
      return ptr_.get();
    }
    bool type(ExportType key) const noexcept {
      return exports_ & mask_(key);
    }
    void type(ExportType key, bool val) const noexcept {
      exports_ = val ? exports_ | mask_(key) : exports_ & ~mask_(key);
    }
  };

  /// Embeds the export-type list into the stat-ptr control-block-pointer.
  ///
  /// The export-type list uses the high 5 bits of the control-block pointer.
  /// This assumption is valid on most platforms under most configurations.
  template <class StatType>
  class StatPtrCompress : private StatPtrBase<StatType> {
   private:
    using Sp = std::shared_ptr<StatType>;
    /// The layout of std::shared_ptr in all major library implementations.
    struct SpLayout {
      StatType* raw{};
      mutable uintptr_t ctl{};
    };

    SpLayout rep_;

    using StatPtrBase<StatType>::mask_;
    using StatPtrBase<StatType>::types_mask;

    static Sp& cast(SpLayout& rep) noexcept {
      FOLLY_PUSH_WARNING
      FOLLY_GCC_DISABLE_WARNING("-Wstrict-aliasing")
      return reinterpret_cast<Sp&>(rep);
      FOLLY_POP_WARNING
    }

   public:
    StatPtrCompress() = default;
    ~StatPtrCompress() {
      rep_.ctl &= ~types_mask;
      folly::annotate_object_collected(reinterpret_cast<void*>(rep_.ctl));
      cast(rep_).~Sp();
    }
    StatPtrCompress(StatPtrCompress&& that) noexcept
        : rep_{std::exchange(that.rep_, {})} {}
    StatPtrCompress(StatPtrCompress const& that) = delete;
    void operator=(StatPtrCompress&& that) = delete;
    void operator=(StatPtrCompress const& that) = delete;
    explicit operator bool() const noexcept {
      return !!rep_.raw;
    }
    std::shared_ptr<StatType> ptr() const noexcept {
      auto rep = rep_;
      rep.ctl &= ~types_mask;
      return cast(rep);
    }
    void ptr(std::shared_ptr<StatType>&& val) noexcept {
      rep_.ctl &= ~types_mask;
      folly::annotate_object_collected(reinterpret_cast<void*>(rep_.ctl));
      cast(rep_) = std::move(val);
      folly::annotate_object_leaked(reinterpret_cast<void*>(rep_.ctl));
    }
    StatType* raw() const noexcept {
      return rep_.raw;
    }
    bool type(ExportType key) const noexcept {
      return rep_.ctl & mask_(key);
    }
    void type(ExportType key, bool val) const noexcept {
      rep_.ctl = val ? rep_.ctl | mask_(key) : rep_.ctl & ~mask_(key);
    }
  };

  template <class StatType>
  using StatPtr = folly::conditional_t<
      folly::kIsArchAmd64 || folly::kIsArchAArch64,
      StatPtrCompress<StatType>,
      StatPtrFallback<StatType>>;

  template <class StatType>
  struct StatPtrHash : std::hash<std::string_view> {
    using is_transparent = void;
    using std::hash<std::string_view>::operator();
    size_t operator()(StatPtr<StatType> const& stat) const noexcept {
      return (*this)(!stat ? std::string_view{} : stat.raw()->name());
    }
  };

  template <class StatType>
  struct StatPtrKeyEqual {
    using is_transparent = void;
    template <typename A, typename B>
    bool operator()(A const& a, B const& b) const noexcept {
      return cast(a) == cast(b);
    }
    static std::string_view cast(std::string_view const name) noexcept {
      return name;
    }
    static std::string_view cast(StatPtr<StatType> const& stat) noexcept {
      return !stat ? std::string_view{} : stat.raw()->name();
    }
  };

  template <class StatType>
  using StatMap = folly::F14FastSet<
      StatPtr<StatType>,
      StatPtrHash<StatType>,
      StatPtrKeyEqual<StatType>>;

  struct State;

  /*
   * Get the TLTimeseries with the given name.
   *
   * Must be called with the state lock held.
   *
   * Never returns NULL.
   */
  TLTimeseries* getTimeseriesLocked(State& state, folly::StringPiece name);
  TLTimeseries* getTimeseriesLocked(
      State& state,
      folly::StringPiece name,
      ExportType exportType);

  /*
   * Get the TLHistogram with the given name.
   *
   * Must be called with the state lock held.
   *
   * May return NULL if no histogram with this name has been created in the
   * global ExportedHistogramMapImpl.  (If no histogram exists, this function
   * cannot automatically create one without knowing the histogram min, max,
   * and bucket width.)
   */
  std::shared_ptr<TLHistogram> getHistogramLocked(
      State& state,
      folly::StringPiece name);
  TLHistogram* getHistogramLockedPtr(State& state, folly::StringPiece name);
  std::shared_ptr<TLHistogram> createHistogramLocked(
      State& state,
      folly::StringPiece name);

  /*
   * Get the TLCounter with the given name.
   *
   * Must be called with the state lock held.
   *
   * Never returns NULL.
   */
  TLCounter* getCounterLocked(State& state, folly::StringPiece name);

  template <typename StatType, typename Make>
  StatPtr<StatType> const& tryInsertLocked( //
      StatMap<StatType>& map,
      folly::StringPiece name,
      Make make);

  struct State {
    StatMap<TLTimeseries> namedTimeseries_;
    StatMap<TLHistogram> namedHistograms_;
    StatMap<TLCounter> namedCounters_;
  };

  folly::Synchronized<State, NamedMapLock> state_;
};

} // namespace facebook::fb303

#include <fb303/ThreadLocalStatsMap-inl.h>
