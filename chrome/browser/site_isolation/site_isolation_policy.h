// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SITE_ISOLATION_SITE_ISOLATION_POLICY_H_
#define CHROME_BROWSER_SITE_ISOLATION_SITE_ISOLATION_POLICY_H_

#include "base/macros.h"

// A centralized place for making policy decisions about site isolation modes
// at the chrome/ layer.  This supplements content::SiteIsolationPolicy with
// features that are specific to chrome/.
//
// These methods can be called from any thread.
class SiteIsolationPolicy {
 public:
  // Returns true if the site isolation mode for isolating sites where users
  // enter passwords is enabled.
  static bool IsIsolationForPasswordSitesEnabled();

  // Returns true if Site Isolation related enterprise policies should take
  // effect (e.g. such policies might not be applicable to low-end Android
  // devices because of 1) performance impact and 2) infeasibility of
  // Spectre-like attacks on such devices).
  static bool IsEnterprisePolicyApplicable();

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(SiteIsolationPolicy);
};

#endif  // CHROME_BROWSER_SITE_ISOLATION_SITE_ISOLATION_POLICY_H_
