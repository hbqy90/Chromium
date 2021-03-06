// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/autocomplete/search_provider.h"

#include "base/string_util.h"
#include "base/time.h"
#include "base/utf_string_conversions.h"
#include "build/build_config.h"
#include "chrome/browser/autocomplete/autocomplete.h"
#include "chrome/browser/autocomplete/autocomplete_match.h"
#include "chrome/browser/history/history.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/search_engines/template_url.h"
#include "chrome/browser/search_engines/template_url_service.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_profile.h"
#include "content/test/test_browser_thread.h"
#include "content/test/test_url_fetcher_factory.h"
#include "net/url_request/url_request_status.h"
#include "testing/gtest/include/gtest/gtest.h"

using content::BrowserThread;

// The following environment is configured for these tests:
// . The TemplateURL default_t_url_ is set as the default provider.
// . The TemplateURL keyword_t_url_ is added to the TemplateURLService. This
//   TemplateURL has a valid suggest and search URL.
// . The URL created by using the search term term1_ with default_t_url_ is
//   added to history.
// . The URL created by using the search term keyword_term_ with keyword_t_url_
//   is added to history.
// . test_factory_ is set as the URLFetcherFactory.
class SearchProviderTest : public testing::Test,
                           public AutocompleteProvider::ACProviderListener {
 public:
  SearchProviderTest()
      : default_t_url_(NULL),
        term1_(UTF8ToUTF16("term1")),
        keyword_t_url_(NULL),
        keyword_term_(UTF8ToUTF16("keyword")),
        io_thread_(BrowserThread::IO),
        quit_when_done_(false) {
    io_thread_.Start();
  }

  // See description above class for what this registers.
  virtual void SetUp();

  virtual void TearDown();

 protected:
  // Adds a search for |term|, using the engine |t_url| to the history, and
  // returns the URL for that search.
  GURL AddSearchToHistory(TemplateURL* t_url, string16 term, int visit_count);

  // Looks for a match in |provider_| with |contents| equal to |contents|.
  // Sets |match| to it if found.  Returns whether |match| was set.
  bool FindMatchWithContents(const string16& contents,
                             AutocompleteMatch* match);

  // Looks for a match in |provider_| with destination |url|.  Sets |match| to
  // it if found.  Returns whether |match| was set.
  bool FindMatchWithDestination(const GURL& url, AutocompleteMatch* match);

  // ACProviderListener method. If we're waiting for the provider to finish,
  // this exits the message loop.
  virtual void OnProviderUpdate(bool updated_matches);

  // Runs a nested message loop until provider_ is done. The message loop is
  // exited by way of OnProviderUPdate.
  void RunTillProviderDone();

  // Invokes Start on provider_, then runs all pending tasks.
  void QueryForInput(const string16& text, bool prevent_inline_autocomplete);

  // Calls QueryForInput(), finishes any suggest query, then if |wyt_match| is
  // non-NULL, sets it to the "what you typed" entry for |text|.
  void QueryForInputAndSetWYTMatch(const string16& text,
                                   AutocompleteMatch* wyt_match);

  // Notifies the URLFetcher for the suggest query corresponding to the default
  // search provider that it's done.
  // Be sure and wrap calls to this in ASSERT_NO_FATAL_FAILURE.
  void FinishDefaultSuggestQuery();

  // See description above class for details of these fields.
  TemplateURL* default_t_url_;
  const string16 term1_;
  GURL term1_url_;
  TemplateURL* keyword_t_url_;
  const string16 keyword_term_;
  GURL keyword_url_;

  MessageLoopForUI message_loop_;
  content::TestBrowserThread io_thread_;

  // URLFetcherFactory implementation registered.
  TestURLFetcherFactory test_factory_;

  // Profile we use.
  TestingProfile profile_;

  // The provider.
  scoped_refptr<SearchProvider> provider_;

  // If true, OnProviderUpdate exits out of the current message loop.
  bool quit_when_done_;

  DISALLOW_COPY_AND_ASSIGN(SearchProviderTest);
};

void SearchProviderTest::SetUp() {
  SearchProvider::set_query_suggest_immediately(true);

  // We need both the history service and template url model loaded.
  profile_.CreateHistoryService(true, false);
  profile_.CreateTemplateURLService();

  TemplateURLService* turl_model =
      TemplateURLServiceFactory::GetForProfile(&profile_);

  turl_model->Load();

  // Reset the default TemplateURL.
  TemplateURLData data;
  data.short_name = ASCIIToUTF16("t");
  data.SetURL("http://defaultturl/{searchTerms}");
  data.suggestions_url = "http://defaultturl2/{searchTerms}";
  default_t_url_ = new TemplateURL(&profile_, data);
  turl_model->Add(default_t_url_);
  turl_model->SetDefaultSearchProvider(default_t_url_);
  TemplateURLID default_provider_id = default_t_url_->id();
  ASSERT_NE(0, default_provider_id);

  // Add url1, with search term term1_.
  term1_url_ = AddSearchToHistory(default_t_url_, term1_, 1);

  // Create another TemplateURL.
  data.short_name = ASCIIToUTF16("k");
  data.SetKeyword(ASCIIToUTF16("k"));
  data.SetURL("http://keyword/{searchTerms}");
  data.suggestions_url = "http://suggest_keyword/{searchTerms}";
  keyword_t_url_ = new TemplateURL(&profile_, data);
  turl_model->Add(keyword_t_url_);
  ASSERT_NE(0, keyword_t_url_->id());

  // Add a page and search term for keyword_t_url_.
  keyword_url_ = AddSearchToHistory(keyword_t_url_, keyword_term_, 1);

  // Keywords are updated by the InMemoryHistoryBackend only after the message
  // has been processed on the history thread. Block until history processes all
  // requests to ensure the InMemoryDatabase is the state we expect it.
  profile_.BlockUntilHistoryProcessesPendingRequests();

  provider_ = new SearchProvider(this, &profile_);
}

void SearchProviderTest::OnProviderUpdate(bool updated_matches) {
  if (quit_when_done_ && provider_->done()) {
    quit_when_done_ = false;
    message_loop_.Quit();
  }
}

void SearchProviderTest::RunTillProviderDone() {
  if (provider_->done())
    return;

  quit_when_done_ = true;
#if defined(OS_MACOSX)
  message_loop_.Run();
#elif defined(OS_ANDROID)
  // Android doesn't have Run(), only Start().
  message_loop_.Start();
#else
  message_loop_.RunWithDispatcher(NULL);
#endif
}

void SearchProviderTest::QueryForInput(const string16& text,
                                       bool prevent_inline_autocomplete) {
  // Start a query.
  AutocompleteInput input(text, string16(), prevent_inline_autocomplete,
                          false, true, AutocompleteInput::ALL_MATCHES);
  provider_->Start(input, false);

  // RunAllPending so that the task scheduled by SearchProvider to create the
  // URLFetchers runs.
  message_loop_.RunAllPending();
}

void SearchProviderTest::QueryForInputAndSetWYTMatch(
    const string16& text,
    AutocompleteMatch* wyt_match) {
  QueryForInput(text, false);
  profile_.BlockUntilHistoryProcessesPendingRequests();
  ASSERT_NO_FATAL_FAILURE(FinishDefaultSuggestQuery());
  EXPECT_NE(profile_.GetPrefs()->GetBoolean(prefs::kInstantEnabled),
            provider_->done());
  if (!wyt_match)
    return;
  ASSERT_GE(provider_->matches().size(), 1u);
  EXPECT_TRUE(FindMatchWithDestination(GURL(
      default_t_url_->url_ref().ReplaceSearchTerms(text,
          TemplateURLRef::NO_SUGGESTIONS_AVAILABLE, string16())), wyt_match));
}

void SearchProviderTest::TearDown() {
  message_loop_.RunAllPending();

  // Shutdown the provider before the profile.
  provider_ = NULL;
}

GURL SearchProviderTest::AddSearchToHistory(TemplateURL* t_url,
                                            string16 term,
                                            int visit_count) {
  HistoryService* history =
      profile_.GetHistoryService(Profile::EXPLICIT_ACCESS);
  GURL search(t_url->url_ref().ReplaceSearchTerms(term,
      TemplateURLRef::NO_SUGGESTIONS_AVAILABLE, string16()));
  static base::Time last_added_time;
  last_added_time = std::max(base::Time::Now(),
      last_added_time + base::TimeDelta::FromMicroseconds(1));
  history->AddPageWithDetails(search, string16(), visit_count, visit_count,
      last_added_time, false, history::SOURCE_BROWSED);
  history->SetKeywordSearchTermsForURL(search, t_url->id(), term);
  return search;
}

bool SearchProviderTest::FindMatchWithContents(const string16& contents,
                                               AutocompleteMatch* match) {
  for (ACMatches::const_iterator i = provider_->matches().begin();
       i != provider_->matches().end(); ++i) {
    if (i->contents == contents) {
      *match = *i;
      return true;
    }
  }
  return false;
}

bool SearchProviderTest::FindMatchWithDestination(const GURL& url,
                                                  AutocompleteMatch* match) {
  for (ACMatches::const_iterator i = provider_->matches().begin();
       i != provider_->matches().end(); ++i) {
    if (i->destination_url == url) {
      *match = *i;
      return true;
    }
  }
  return false;
}

void SearchProviderTest::FinishDefaultSuggestQuery() {
  TestURLFetcher* default_fetcher = test_factory_.GetFetcherByID(
      SearchProvider::kDefaultProviderURLFetcherID);
  ASSERT_TRUE(default_fetcher);

  // Tell the SearchProvider the default suggest query is done.
  default_fetcher->set_response_code(200);
  default_fetcher->delegate()->OnURLFetchComplete(default_fetcher);
}

// Tests -----------------------------------------------------------------------

// Make sure we query history for the default provider and a URLFetcher is
// created for the default provider suggest results.
TEST_F(SearchProviderTest, QueryDefaultProvider) {
  string16 term = term1_.substr(0, term1_.length() - 1);
  QueryForInput(term, false);

  // Make sure the default providers suggest service was queried.
  TestURLFetcher* fetcher = test_factory_.GetFetcherByID(
      SearchProvider::kDefaultProviderURLFetcherID);
  ASSERT_TRUE(fetcher);

  // And the URL matches what we expected.
  GURL expected_url(default_t_url_->suggestions_url_ref().ReplaceSearchTerms(
      term, TemplateURLRef::NO_SUGGESTIONS_AVAILABLE, string16()));
  ASSERT_TRUE(fetcher->GetOriginalURL() == expected_url);

  // Tell the SearchProvider the suggest query is done.
  fetcher->set_response_code(200);
  fetcher->delegate()->OnURLFetchComplete(fetcher);
  fetcher = NULL;

  // Run till the history results complete.
  RunTillProviderDone();

  // The SearchProvider is done. Make sure it has a result for the history
  // term term1.
  AutocompleteMatch term1_match;
  EXPECT_TRUE(FindMatchWithDestination(term1_url_, &term1_match));
  // Term1 should not have a description, it's set later.
  EXPECT_TRUE(term1_match.description.empty());

  AutocompleteMatch wyt_match;
  EXPECT_TRUE(FindMatchWithDestination(
      GURL(default_t_url_->url_ref().ReplaceSearchTerms(term,
          TemplateURLRef::NO_SUGGESTIONS_AVAILABLE, string16())), &wyt_match));
  EXPECT_TRUE(wyt_match.description.empty());

  // The match for term1 should be more relevant than the what you typed result.
  EXPECT_GT(term1_match.relevance, wyt_match.relevance);
}

TEST_F(SearchProviderTest, HonorPreventInlineAutocomplete) {
  string16 term = term1_.substr(0, term1_.length() - 1);
  QueryForInput(term, true);

  ASSERT_FALSE(provider_->matches().empty());
  ASSERT_EQ(AutocompleteMatch::SEARCH_WHAT_YOU_TYPED,
            provider_->matches()[0].type);
}

// Issues a query that matches the registered keyword and makes sure history
// is queried as well as URLFetchers getting created.
TEST_F(SearchProviderTest, QueryKeywordProvider) {
  string16 term = keyword_term_.substr(0, keyword_term_.length() - 1);
  QueryForInput(keyword_t_url_->keyword() + UTF8ToUTF16(" ") + term, false);

  // Make sure the default providers suggest service was queried.
  TestURLFetcher* default_fetcher = test_factory_.GetFetcherByID(
      SearchProvider::kDefaultProviderURLFetcherID);
  ASSERT_TRUE(default_fetcher);

  // Tell the SearchProvider the default suggest query is done.
  default_fetcher->set_response_code(200);
  default_fetcher->delegate()->OnURLFetchComplete(default_fetcher);
  default_fetcher = NULL;

  // Make sure the keyword providers suggest service was queried.
  TestURLFetcher* keyword_fetcher = test_factory_.GetFetcherByID(
      SearchProvider::kKeywordProviderURLFetcherID);
  ASSERT_TRUE(keyword_fetcher);

  // And the URL matches what we expected.
  GURL expected_url(keyword_t_url_->suggestions_url_ref().ReplaceSearchTerms(
      term, TemplateURLRef::NO_SUGGESTIONS_AVAILABLE, string16()));
  ASSERT_TRUE(keyword_fetcher->GetOriginalURL() == expected_url);

  // Tell the SearchProvider the keyword suggest query is done.
  keyword_fetcher->set_response_code(200);
  keyword_fetcher->delegate()->OnURLFetchComplete(keyword_fetcher);
  keyword_fetcher = NULL;

  // Run till the history results complete.
  RunTillProviderDone();

  // The SearchProvider is done. Make sure it has a result for the history
  // term keyword.
  AutocompleteMatch match;
  EXPECT_TRUE(FindMatchWithDestination(keyword_url_, &match));

  // The match should have an associated keyword.
  EXPECT_FALSE(match.keyword.empty());

  // The fill into edit should contain the keyword.
  EXPECT_EQ(keyword_t_url_->keyword() + char16(' ') + keyword_term_,
            match.fill_into_edit);
}

TEST_F(SearchProviderTest, DontSendPrivateDataToSuggest) {
  // None of the following input strings should be sent to the suggest server,
  // because they may contain private data.
  const char* inputs[] = {
    "username:password",
    "http://username:password",
    "https://username:password",
    "username:password@hostname",
    "http://username:password@hostname/",
    "file://filename",
    "data://data",
    "unknownscheme:anything",
    "http://hostname/?query=q",
    "http://hostname/path#ref",
    "https://hostname/path",
  };

  for (size_t i = 0; i < arraysize(inputs); ++i) {
    QueryForInput(ASCIIToUTF16(inputs[i]), false);
    // Make sure the default providers suggest service was not queried.
    ASSERT_TRUE(test_factory_.GetFetcherByID(
        SearchProvider::kDefaultProviderURLFetcherID) == NULL);
    // Run till the history results complete.
    RunTillProviderDone();
  }
}

// Make sure FinalizeInstantQuery works.
TEST_F(SearchProviderTest, FinalizeInstantQuery) {
  PrefService* service = profile_.GetPrefs();
  service->SetBoolean(prefs::kInstantEnabled, true);

  ASSERT_NO_FATAL_FAILURE(QueryForInputAndSetWYTMatch(ASCIIToUTF16("foo"),
                                                      NULL));

  // Tell the provider instant is done.
  provider_->FinalizeInstantQuery(ASCIIToUTF16("foo"), ASCIIToUTF16("bar"));

  // The provider should now be done.
  EXPECT_TRUE(provider_->done());

  // There should be two matches, one for what you typed, the other for
  // 'foobar'.
  EXPECT_EQ(2u, provider_->matches().size());
  GURL instant_url(default_t_url_->url_ref().ReplaceSearchTerms(
      ASCIIToUTF16("foobar"), TemplateURLRef::NO_SUGGESTIONS_AVAILABLE,
      string16()));
  AutocompleteMatch instant_match;
  EXPECT_TRUE(FindMatchWithDestination(instant_url, &instant_match));

  // And the 'foobar' match should not have a description, it'll be set later.
  EXPECT_TRUE(instant_match.description.empty());

  // Make sure the what you typed match has no description.
  AutocompleteMatch wyt_match;
  EXPECT_TRUE(FindMatchWithDestination(
      GURL(default_t_url_->url_ref().ReplaceSearchTerms(ASCIIToUTF16("foo"),
          TemplateURLRef::NO_SUGGESTIONS_AVAILABLE, string16())), &wyt_match));
  EXPECT_TRUE(wyt_match.description.empty());

  // The instant search should be more relevant.
  EXPECT_GT(instant_match.relevance, wyt_match.relevance);
}

// Make sure that if FinalizeInstantQuery is invoked before suggest results
// return, the suggest text from FinalizeInstantQuery is remembered.
TEST_F(SearchProviderTest, RememberInstantQuery) {
  PrefService* service = profile_.GetPrefs();
  service->SetBoolean(prefs::kInstantEnabled, true);

  QueryForInput(ASCIIToUTF16("foo"), false);

  // Finalize the instant query immediately.
  provider_->FinalizeInstantQuery(ASCIIToUTF16("foo"), ASCIIToUTF16("bar"));

  // There should be two matches, one for what you typed, the other for
  // 'foobar'.
  EXPECT_EQ(2u, provider_->matches().size());
  GURL instant_url(default_t_url_->url_ref().ReplaceSearchTerms(
      ASCIIToUTF16("foobar"), TemplateURLRef::NO_SUGGESTIONS_AVAILABLE,
      string16()));
  AutocompleteMatch instant_match;
  EXPECT_TRUE(FindMatchWithDestination(instant_url, &instant_match));

  // Wait until history and the suggest query complete.
  profile_.BlockUntilHistoryProcessesPendingRequests();
  ASSERT_NO_FATAL_FAILURE(FinishDefaultSuggestQuery());

  // Provider should be done.
  EXPECT_TRUE(provider_->done());

  // There should be two matches, one for what you typed, the other for
  // 'foobar'.
  EXPECT_EQ(2u, provider_->matches().size());
  EXPECT_TRUE(FindMatchWithDestination(instant_url, &instant_match));

  // And the 'foobar' match should not have a description, it'll be set later.
  EXPECT_TRUE(instant_match.description.empty());
}

// Make sure that if trailing whitespace is added to the text supplied to
// AutocompleteInput the default suggest text is cleared.
TEST_F(SearchProviderTest, DifferingText) {
  PrefService* service = profile_.GetPrefs();
  service->SetBoolean(prefs::kInstantEnabled, true);

  ASSERT_NO_FATAL_FAILURE(QueryForInputAndSetWYTMatch(ASCIIToUTF16("foo"),
                                                      NULL));

  // Finalize the instant query immediately.
  provider_->FinalizeInstantQuery(ASCIIToUTF16("foo"), ASCIIToUTF16("bar"));

  // Query with the same input text, but trailing whitespace.
  AutocompleteMatch instant_match;
  ASSERT_NO_FATAL_FAILURE(QueryForInputAndSetWYTMatch(ASCIIToUTF16("foo "),
                                                      &instant_match));

  // There should only one match, for what you typed.
  EXPECT_EQ(1u, provider_->matches().size());
  EXPECT_FALSE(instant_match.destination_url.is_empty());
}

TEST_F(SearchProviderTest, DontAutocompleteURLLikeTerms) {
  profile_.CreateAutocompleteClassifier();
  GURL url = AddSearchToHistory(default_t_url_,
                                ASCIIToUTF16("docs.google.com"), 1);

  // Add the term as a url.
  profile_.GetHistoryService(Profile::EXPLICIT_ACCESS)->AddPageWithDetails(
      GURL("http://docs.google.com"), string16(), 1, 1, base::Time::Now(),
      false, history::SOURCE_BROWSED);
  profile_.BlockUntilHistoryProcessesPendingRequests();

  AutocompleteMatch wyt_match;
  ASSERT_NO_FATAL_FAILURE(QueryForInputAndSetWYTMatch(ASCIIToUTF16("docs"),
                                                      &wyt_match));

  // There should be two matches, one for what you typed, the other for
  // 'docs.google.com'. The search term should have a lower priority than the
  // what you typed match.
  ASSERT_EQ(2u, provider_->matches().size());
  AutocompleteMatch term_match;
  EXPECT_TRUE(FindMatchWithDestination(url, &term_match));
  EXPECT_GT(wyt_match.relevance, term_match.relevance);
}

// A multiword search with one visit should not autocomplete until multiple
// words are typed.
TEST_F(SearchProviderTest, DontAutocompleteUntilMultipleWordsTyped) {
  GURL term_url(AddSearchToHistory(default_t_url_, ASCIIToUTF16("one search"),
                                   1));
  profile_.BlockUntilHistoryProcessesPendingRequests();

  AutocompleteMatch wyt_match;
  ASSERT_NO_FATAL_FAILURE(QueryForInputAndSetWYTMatch(ASCIIToUTF16("on"),
                                                      &wyt_match));
  ASSERT_EQ(2u, provider_->matches().size());
  AutocompleteMatch term_match;
  EXPECT_TRUE(FindMatchWithDestination(term_url, &term_match));
  EXPECT_GT(wyt_match.relevance, term_match.relevance);

  ASSERT_NO_FATAL_FAILURE(QueryForInputAndSetWYTMatch(ASCIIToUTF16("one se"),
                                                      &wyt_match));
  ASSERT_EQ(2u, provider_->matches().size());
  EXPECT_TRUE(FindMatchWithDestination(term_url, &term_match));
  EXPECT_GT(term_match.relevance, wyt_match.relevance);
}

// A multiword search with more than one visit should autocomplete immediately.
TEST_F(SearchProviderTest, AutocompleteMultipleVisitsImmediately) {
  GURL term_url(AddSearchToHistory(default_t_url_, ASCIIToUTF16("two searches"),
                                   2));
  profile_.BlockUntilHistoryProcessesPendingRequests();

  AutocompleteMatch wyt_match;
  ASSERT_NO_FATAL_FAILURE(QueryForInputAndSetWYTMatch(ASCIIToUTF16("tw"),
                                                      &wyt_match));
  ASSERT_EQ(2u, provider_->matches().size());
  AutocompleteMatch term_match;
  EXPECT_TRUE(FindMatchWithDestination(term_url, &term_match));
  EXPECT_GT(term_match.relevance, wyt_match.relevance);
}

// Autocompletion should work at a word boundary after a space.
TEST_F(SearchProviderTest, AutocompleteAfterSpace) {
  GURL term_url(AddSearchToHistory(default_t_url_, ASCIIToUTF16("two searches"),
                                   2));
  profile_.BlockUntilHistoryProcessesPendingRequests();

  AutocompleteMatch wyt_match;
  ASSERT_NO_FATAL_FAILURE(QueryForInputAndSetWYTMatch(ASCIIToUTF16("two "),
                                                      &wyt_match));
  ASSERT_EQ(2u, provider_->matches().size());
  AutocompleteMatch term_match;
  EXPECT_TRUE(FindMatchWithDestination(term_url, &term_match));
  EXPECT_GT(term_match.relevance, wyt_match.relevance);
}

// Newer multiword searches should score more highly than older ones.
TEST_F(SearchProviderTest, ScoreNewerSearchesHigher) {
  GURL term_url_a(AddSearchToHistory(default_t_url_,
                                     ASCIIToUTF16("three searches aaa"), 1));
  GURL term_url_b(AddSearchToHistory(default_t_url_,
                                     ASCIIToUTF16("three searches bbb"), 1));
  profile_.BlockUntilHistoryProcessesPendingRequests();

  AutocompleteMatch wyt_match;
  ASSERT_NO_FATAL_FAILURE(QueryForInputAndSetWYTMatch(ASCIIToUTF16("three se"),
                                                      &wyt_match));
  ASSERT_EQ(3u, provider_->matches().size());
  AutocompleteMatch term_match_a;
  EXPECT_TRUE(FindMatchWithDestination(term_url_a, &term_match_a));
  AutocompleteMatch term_match_b;
  EXPECT_TRUE(FindMatchWithDestination(term_url_b, &term_match_b));
  EXPECT_GT(term_match_b.relevance, term_match_a.relevance);
  EXPECT_GT(term_match_a.relevance, wyt_match.relevance);
}

// An autocompleted multiword search should not be replaced by a different
// autocompletion while the user is still typing a valid prefix.
TEST_F(SearchProviderTest, DontReplacePreviousAutocompletion) {
  GURL term_url_a(AddSearchToHistory(default_t_url_,
                                     ASCIIToUTF16("four searches aaa"), 2));
  GURL term_url_b(AddSearchToHistory(default_t_url_,
                                     ASCIIToUTF16("four searches bbb"), 1));
  profile_.BlockUntilHistoryProcessesPendingRequests();

  AutocompleteMatch wyt_match;
  ASSERT_NO_FATAL_FAILURE(QueryForInputAndSetWYTMatch(ASCIIToUTF16("fo"),
                                                      &wyt_match));
  ASSERT_EQ(3u, provider_->matches().size());
  AutocompleteMatch term_match_a;
  EXPECT_TRUE(FindMatchWithDestination(term_url_a, &term_match_a));
  AutocompleteMatch term_match_b;
  EXPECT_TRUE(FindMatchWithDestination(term_url_b, &term_match_b));
  EXPECT_GT(term_match_a.relevance, wyt_match.relevance);
  EXPECT_GT(wyt_match.relevance, term_match_b.relevance);

  ASSERT_NO_FATAL_FAILURE(QueryForInputAndSetWYTMatch(ASCIIToUTF16("four se"),
                                                      &wyt_match));
  ASSERT_EQ(3u, provider_->matches().size());
  EXPECT_TRUE(FindMatchWithDestination(term_url_a, &term_match_a));
  EXPECT_TRUE(FindMatchWithDestination(term_url_b, &term_match_b));
  EXPECT_GT(term_match_a.relevance, wyt_match.relevance);
  EXPECT_GT(wyt_match.relevance, term_match_b.relevance);
}

// Non-completable multiword searches should not crowd out single-word searches.
TEST_F(SearchProviderTest, DontCrowdOutSingleWords) {
  GURL term_url(AddSearchToHistory(default_t_url_, ASCIIToUTF16("five"), 1));
  AddSearchToHistory(default_t_url_, ASCIIToUTF16("five searches bbb"), 1);
  AddSearchToHistory(default_t_url_, ASCIIToUTF16("five searches ccc"), 1);
  AddSearchToHistory(default_t_url_, ASCIIToUTF16("five searches ddd"), 1);
  AddSearchToHistory(default_t_url_, ASCIIToUTF16("five searches eee"), 1);
  profile_.BlockUntilHistoryProcessesPendingRequests();

  AutocompleteMatch wyt_match;
  ASSERT_NO_FATAL_FAILURE(QueryForInputAndSetWYTMatch(ASCIIToUTF16("fi"),
                                                      &wyt_match));
  ASSERT_EQ(AutocompleteProvider::kMaxMatches + 1, provider_->matches().size());
  AutocompleteMatch term_match;
  EXPECT_TRUE(FindMatchWithDestination(term_url, &term_match));
  EXPECT_GT(term_match.relevance, wyt_match.relevance);
}

// Verifies AutocompleteControllers sets descriptions for results correctly.
TEST_F(SearchProviderTest, UpdateKeywordDescriptions) {
  // Add an entry that corresponds to a keyword search with 'term2'.
  AddSearchToHistory(keyword_t_url_, ASCIIToUTF16("term2"), 1);
  profile_.BlockUntilHistoryProcessesPendingRequests();

  ACProviders providers;
  SearchProvider* provider = provider_.release();
  providers.push_back(provider);
  AutocompleteController controller(providers, &profile_);
  controller.set_search_provider(provider);
  provider->set_listener(&controller);
  controller.Start(ASCIIToUTF16("k t"), string16(), false, false, true,
                   AutocompleteInput::ALL_MATCHES);
  const AutocompleteResult& result = controller.result();

  // There should be two matches, one for the keyword one for what you typed.
  ASSERT_EQ(2u, result.size());

  EXPECT_FALSE(result.match_at(0).keyword.empty());
  EXPECT_FALSE(result.match_at(1).keyword.empty());
  EXPECT_NE(result.match_at(0).keyword, result.match_at(1).keyword);

  EXPECT_FALSE(result.match_at(0).description.empty());
  EXPECT_FALSE(result.match_at(1).description.empty());
  EXPECT_NE(result.match_at(0).description, result.match_at(1).description);
}

// Verifies Navsuggest results don't set a TemplateURL, which instant relies on.
// Also verifies that just the *first* navigational result is listed as a match.
TEST_F(SearchProviderTest, NavSuggest) {
  QueryForInput(ASCIIToUTF16("a.c"), false);

  // Make sure the default providers suggest service was queried.
  TestURLFetcher* fetcher = test_factory_.GetFetcherByID(
      SearchProvider::kDefaultProviderURLFetcherID);
  ASSERT_TRUE(fetcher);

  // Tell the SearchProvider the suggest query is done.
  fetcher->set_response_code(200);
  fetcher->SetResponseString(
      "[\"a.c\",[\"a.com\", \"a.com/b\"],[\"a\", \"b\"],[],"
      "{\"google:suggesttype\":[\"NAVIGATION\", \"NAVIGATION\"]}]");
  fetcher->delegate()->OnURLFetchComplete(fetcher);
  fetcher = NULL;

  // Run till the history results complete.
  RunTillProviderDone();

  // Make sure the only match is 'a.com' and it doesn't have a template_url.
  AutocompleteMatch nav_match;
  EXPECT_TRUE(FindMatchWithDestination(GURL("http://a.com"), &nav_match));
  EXPECT_TRUE(nav_match.keyword.empty());
  EXPECT_FALSE(FindMatchWithDestination(GURL("http://a.com/b"), &nav_match));
}

// Verifies that the most relevant suggest results are added properly.
TEST_F(SearchProviderTest, SuggestRelevance) {
  QueryForInput(ASCIIToUTF16("a"), false);

  // Make sure the default provider's suggest service was queried.
  TestURLFetcher* fetcher = test_factory_.GetFetcherByID(
      SearchProvider::kDefaultProviderURLFetcherID);
  ASSERT_TRUE(fetcher);

  // Tell the SearchProvider the suggest query is done.
  fetcher->set_response_code(200);
  fetcher->SetResponseString("[\"a\",[\"a1\", \"a2\", \"a3\", \"a4\"]]");
  fetcher->delegate()->OnURLFetchComplete(fetcher);
  fetcher = NULL;

  // Run till the history results complete.
  RunTillProviderDone();

  // Check the expected verbatim and (first 3) suggestions' relative relevances.
  AutocompleteMatch verbatim, match_a1, match_a2, match_a3, match_a4;
  EXPECT_TRUE(FindMatchWithContents(ASCIIToUTF16("a"), &verbatim));
  EXPECT_TRUE(FindMatchWithContents(ASCIIToUTF16("a1"), &match_a1));
  EXPECT_TRUE(FindMatchWithContents(ASCIIToUTF16("a2"), &match_a2));
  EXPECT_TRUE(FindMatchWithContents(ASCIIToUTF16("a3"), &match_a3));
  EXPECT_FALSE(FindMatchWithContents(ASCIIToUTF16("a4"), &match_a4));
  EXPECT_GT(verbatim.relevance, match_a1.relevance);
  EXPECT_GT(match_a1.relevance, match_a2.relevance);
  EXPECT_GT(match_a2.relevance, match_a3.relevance);
}

// Verifies inline autocompletion of navigational results.
TEST_F(SearchProviderTest, NavigationInline) {
  struct {
    const std::string input;
    const std::string url;
    // Test the expected fill_into_edit, which may drop "http://".
    // Some cases will not trim "http://" because "www." is not a valid prefix.
    const std::string fill_into_edit;
    size_t inline_offset;
  } cases[] = {
    // Do not inline matches that do not contain the input; trim http as needed.
    { "x",                    "http://www.abc.com",
                                     "www.abc.com",  string16::npos },
    { "https:",               "http://www.abc.com",
                                     "www.abc.com",  string16::npos },
    { "abc.com/",             "http://www.abc.com",
                                     "www.abc.com",  string16::npos },
    { "http://www.abc.com/a", "http://www.abc.com",
                              "http://www.abc.com",  string16::npos },
    { "http://www.abc.com",   "https://www.abc.com",
                              "https://www.abc.com", string16::npos },
    { "http://abc.com",       "ftp://abc.com",
                              "ftp://abc.com",       string16::npos },
    { "https://www.abc.com",  "http://www.abc.com",
                                     "www.abc.com",  string16::npos },
    { "ftp://abc.com",        "http://abc.com",
                                     "abc.com",      string16::npos },

    // Do not inline matches with invalid input prefixes; trim http as needed.
    { "ttp",                  "http://www.abc.com",
                                     "www.abc.com", string16::npos },
    { "://w",                 "http://www.abc.com",
                                     "www.abc.com", string16::npos },
    { "ww.",                  "http://www.abc.com",
                                     "www.abc.com", string16::npos },
    { ".ab",                  "http://www.abc.com",
                                     "www.abc.com", string16::npos },
    { "bc",                   "http://www.abc.com",
                                     "www.abc.com", string16::npos },
    { ".com",                 "http://www.abc.com",
                                     "www.abc.com", string16::npos },

    // Do not inline matches that omit input domain labels; trim http as needed.
    { "www.a",                "http://a.com",
                                     "a.com",       string16::npos },
    { "http://www.a",         "http://a.com",
                              "http://a.com",       string16::npos },
    { "www.a",                "ftp://a.com",
                              "ftp://a.com",        string16::npos },
    { "ftp://www.a",          "ftp://a.com",
                              "ftp://a.com",        string16::npos },

    // Input matching but with nothing to inline will not yield an offset.
    { "abc.com",              "http://www.abc.com",
                                     "www.abc.com", string16::npos },
    { "http://www.abc.com",   "http://www.abc.com",
                              "http://www.abc.com", string16::npos },

    // Inline matches when the input is a leading substring of the scheme.
    { "h",                    "http://www.abc.com",
                              "http://www.abc.com", 1 },
    { "http",                 "http://www.abc.com",
                              "http://www.abc.com", 4 },

    // Inline matches when the input is a leading substring of the full URL.
    { "http:",                "http://www.abc.com",
                              "http://www.abc.com", 5 },
    { "http://w",             "http://www.abc.com",
                              "http://www.abc.com", 8 },
    { "http://www.",          "http://www.abc.com",
                              "http://www.abc.com", 11 },
    { "http://www.ab",        "http://www.abc.com",
                              "http://www.abc.com", 13 },
    { "http://www.abc.com/p", "http://www.abc.com/path/file.htm?q=x#foo",
                              "http://www.abc.com/path/file.htm?q=x#foo", 20 },
    { "http://abc.com/p",     "http://abc.com/path/file.htm?q=x#foo",
                              "http://abc.com/path/file.htm?q=x#foo",     16 },

    // Inline matches with valid URLPrefixes; only trim "http://".
    { "w",                    "http://www.abc.com",
                                     "www.abc.com", 1 },
    { "www.a",                "http://www.abc.com",
                                     "www.abc.com", 5 },
    { "abc",                  "http://www.abc.com",
                                     "www.abc.com", 7 },
    { "abc.c",                "http://www.abc.com",
                                     "www.abc.com", 9 },
    { "abc.com/p",            "http://www.abc.com/path/file.htm?q=x#foo",
                                     "www.abc.com/path/file.htm?q=x#foo", 13 },
    { "abc.com/p",            "http://abc.com/path/file.htm?q=x#foo",
                                     "abc.com/path/file.htm?q=x#foo",     9 },

    // Inline matches using the maximal URLPrefix components.
    { "h",                    "http://help.com",
                                     "help.com",     1 },
    { "http",                 "http://http.com",
                                     "http.com",     4 },
    { "h",                    "http://www.help.com",
                                     "www.help.com", 5 },
    { "http",                 "http://www.http.com",
                                     "www.http.com", 8 },
    { "w",                    "http://www.www.com",
                                     "www.www.com",  5 },

    // Test similar behavior for the ftp and https schemes.
    { "ftp://www.ab",         "ftp://www.abc.com/path/file.htm?q=x#foo",
                              "ftp://www.abc.com/path/file.htm?q=x#foo",   12 },
    { "www.ab",               "ftp://www.abc.com/path/file.htm?q=x#foo",
                              "ftp://www.abc.com/path/file.htm?q=x#foo",   12 },
    { "ab",                   "ftp://www.abc.com/path/file.htm?q=x#foo",
                              "ftp://www.abc.com/path/file.htm?q=x#foo",   12 },
    { "ab",                   "ftp://abc.com/path/file.htm?q=x#foo",
                              "ftp://abc.com/path/file.htm?q=x#foo",       8 },
    { "https://www.ab",       "https://www.abc.com/path/file.htm?q=x#foo",
                              "https://www.abc.com/path/file.htm?q=x#foo", 14 },
    { "www.ab",               "https://www.abc.com/path/file.htm?q=x#foo",
                              "https://www.abc.com/path/file.htm?q=x#foo", 14 },
    { "ab",                   "https://www.abc.com/path/file.htm?q=x#foo",
                              "https://www.abc.com/path/file.htm?q=x#foo", 14 },
    { "ab",                   "https://abc.com/path/file.htm?q=x#foo",
                              "https://abc.com/path/file.htm?q=x#foo",     10 },

    // Forced query input should inline and retain the "?" prefix.
    { "?http://www.ab",       "http://www.abc.com",
                             "?http://www.abc.com", 14 },
    { "?www.ab",              "http://www.abc.com",
                                    "?www.abc.com", 7 },
    { "?ab",                  "http://www.abc.com",
                                    "?www.abc.com", 7 },
  };

  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(cases); i++) {
    QueryForInput(ASCIIToUTF16(cases[i].input), false);
    SearchProvider::NavigationResult result(GURL(cases[i].url), string16(), 0);
    AutocompleteMatch match(provider_->NavigationToMatch(result, false));
    EXPECT_EQ(cases[i].inline_offset, match.inline_autocomplete_offset);
    EXPECT_EQ(ASCIIToUTF16(cases[i].fill_into_edit), match.fill_into_edit);
  }
}

// Verifies that "http://" is not trimmed for input that is a leading substring.
TEST_F(SearchProviderTest, NavigationInlineSchemeSubstring) {
  const string16 input(ASCIIToUTF16("ht"));
  const string16 url(ASCIIToUTF16("http://a.com"));
  const SearchProvider::NavigationResult result(GURL(url), string16(), 0);

  // Check the offset and strings when inline autocompletion is allowed.
  QueryForInput(input, false);
  AutocompleteMatch match_inline(provider_->NavigationToMatch(result, false));
  EXPECT_EQ(2U, match_inline.inline_autocomplete_offset);
  EXPECT_EQ(url, match_inline.fill_into_edit);
  EXPECT_EQ(url, match_inline.contents);

  // Check the same offset and strings when inline autocompletion is prevented.
  QueryForInput(input, true);
  AutocompleteMatch match_prevent(provider_->NavigationToMatch(result, false));
  EXPECT_EQ(string16::npos, match_prevent.inline_autocomplete_offset);
  EXPECT_EQ(url, match_prevent.fill_into_edit);
  EXPECT_EQ(url, match_prevent.contents);
}

// Verifies that input "w" marks a more significant domain label than "www.".
TEST_F(SearchProviderTest, NavigationInlineDomainClassify) {
  QueryForInput(ASCIIToUTF16("w"), false);
  const GURL url("http://www.wow.com");
  const SearchProvider::NavigationResult result(url, string16(), 0);
  AutocompleteMatch match(provider_->NavigationToMatch(result, false));
  EXPECT_EQ(5U, match.inline_autocomplete_offset);
  EXPECT_EQ(ASCIIToUTF16("www.wow.com"), match.fill_into_edit);
  EXPECT_EQ(ASCIIToUTF16("www.wow.com"), match.contents);

  // Ensure that the match for input "w" is marked on "wow" and not "www".
  ASSERT_EQ(3U, match.contents_class.size());
  EXPECT_EQ(0U, match.contents_class[0].offset);
  EXPECT_EQ(AutocompleteMatch::ACMatchClassification::URL,
            match.contents_class[0].style);
  EXPECT_EQ(4U, match.contents_class[1].offset);
  EXPECT_EQ(AutocompleteMatch::ACMatchClassification::URL |
            AutocompleteMatch::ACMatchClassification::MATCH,
            match.contents_class[1].style);
  EXPECT_EQ(5U, match.contents_class[2].offset);
  EXPECT_EQ(AutocompleteMatch::ACMatchClassification::URL,
            match.contents_class[2].style);
}
