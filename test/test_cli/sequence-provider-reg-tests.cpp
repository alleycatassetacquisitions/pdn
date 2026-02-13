#ifdef NATIVE_BUILD

#include "sequence-provider-tests.hpp"

// ============================================
// LOCAL SEQUENCE PROVIDER TESTS
// ============================================

TEST_F(SequenceProviderTestSuite, LocalProviderSignalEchoEasy) {
    localProviderSignalEchoEasy(this);
}

TEST_F(SequenceProviderTestSuite, LocalProviderSignalEchoHard) {
    localProviderSignalEchoHard(this);
}

TEST_F(SequenceProviderTestSuite, LocalProviderGhostRunnerEasy) {
    localProviderGhostRunnerEasy(this);
}

TEST_F(SequenceProviderTestSuite, LocalProviderGhostRunnerHard) {
    localProviderGhostRunnerHard(this);
}

TEST_F(SequenceProviderTestSuite, LocalProviderDeterministic) {
    localProviderDeterministic(this);
}

TEST_F(SequenceProviderTestSuite, LocalProviderSequenceMixed) {
    localProviderSequenceMixed(this);
}

// ============================================
// SERVER SEQUENCE PROVIDER TESTS
// ============================================

TEST_F(SequenceProviderTestSuite, ServerProviderDisconnected) {
    serverProviderDisconnected(this);
}

TEST_F(SequenceProviderTestSuite, ServerProviderTimeoutConfig) {
    serverProviderTimeoutConfig(this);
}

TEST_F(SequenceProviderTestSuite, ServerProviderParseSignalEcho) {
    serverProviderParseSignalEcho(this);
}

// ============================================
// HYBRID SEQUENCE PROVIDER TESTS
// ============================================

TEST_F(SequenceProviderTestSuite, HybridProviderFallback) {
    hybridProviderFallback(this);
}

TEST_F(SequenceProviderTestSuite, HybridProviderStats) {
    hybridProviderStats(this);
}

TEST_F(SequenceProviderTestSuite, HybridProviderTimeout) {
    hybridProviderTimeout(this);
}

// ============================================
// SEQUENCE CACHE TESTS
// ============================================

TEST_F(SequenceProviderTestSuite, CacheStoreRetrieve) {
    cacheStoreRetrieve(this);
}

TEST_F(SequenceProviderTestSuite, CacheHasEntry) {
    cacheHasEntry(this);
}

TEST_F(SequenceProviderTestSuite, CacheClear) {
    cacheClear(this);
}

TEST_F(SequenceProviderTestSuite, CacheMultipleEntries) {
    cacheMultipleEntries(this);
}

// ============================================
// INTEGRATION TESTS
// ============================================

TEST_F(SequenceProviderTestSuite, IntegrationLocalProvider) {
    integrationLocalProvider(this);
}

#endif // NATIVE_BUILD
