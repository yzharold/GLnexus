#ifndef GLNEXUS_BCFBUCKETCACHE_H
#define GLNEXUS_BCFBUCKETCACHE_H
#include "types.h"
#include "KeyValue.h"

namespace GLnexus {

struct BCFBucketCache_body;

/// Cache for BCF buckets. A bucket is stored in
/// memory as a vector of parsed BCF records. Caching helps, for
/// example, in cases where multiple threads access the same buckets.
class BCFBucketCache {
    // pImpl idiom
    std::unique_ptr<BCFBucketCache_body> body_;

    BCFBucketCache();
    BCFBucketCache(const BCFBucketCache&) = delete;

public:
    virtual ~BCFBucketCache();

    static Status Open(KeyValue::DB* db,
                       std::unique_ptr<BCFBucketCache>& ans);

    /// Get a shared read-only pointer to a bucket.
    ///
    /// Look up if this bucket is already cached.  If so, the caller
    /// get a reference to the existing bucket. Otherwise, the bucket
    /// is retrieved from the on disk database, parsed, and added to
    /// the cache. Then, the caller gets a reference.
    ///
    /// The shared reference is thread safe, so the caller can use it
    /// in any thread context. The bucket data is immutable, and
    /// shared between all cache users. The caller must not try to
    /// modify it.
    ///
    /// The return value should be OK, unless an IO error has occurred.
    Status get_bucket(const std::string& key,
                      const std::string& dataset,
                      const range& r,
                      std::shared_ptr<std::vector<std::shared_ptr<bcf1_t> > >& bucket);

};

}

#endif