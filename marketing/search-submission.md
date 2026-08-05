# Search-engine submission checklist

_Prepared 5 August 2026. Authentication and ownership verification must be completed by the GitHub account owner._

## URLs to submit

- Property: `https://rchristie95.github.io/MinkowskiKart/`
- Sitemap: `https://rchristie95.github.io/MinkowskiKart/sitemap.xml`
- Homepage: `https://rchristie95.github.io/MinkowskiKart/`
- Download page: `https://rchristie95.github.io/MinkowskiKart/download.html`
- Physics page: `https://rchristie95.github.io/MinkowskiKart/physics.html`
- About page: `https://rchristie95.github.io/MinkowskiKart/about.html`
- FAQ: `https://rchristie95.github.io/MinkowskiKart/faq.html`

Do not submit a preview URL, repository file URL or branch-specific Pages URL as canonical.

## 1. Preflight the deployed site

Wait for the GitHub Pages deployment to finish, then open every URL above in a private browser window. Confirm that each returns the intended page without authentication or a redirect loop.

Check the raw HTML of each public page for:

- one self-referencing `<link rel="canonical">` using the exact HTTPS URL;
- one unique `<title>` and meta description;
- an indexable robots directive—no `noindex` meta tag or header;
- crawlable links to the other main pages;
- matching Open Graph URL and page identity;
- meaningful image alternative text;
- JSON-LD that describes only visible, verified information.

Confirm that `robots.txt` permits the public pages and contains:

```text
Sitemap: https://rchristie95.github.io/MinkowskiKart/sitemap.xml
```

The sitemap should contain only the canonical URLs above, with accurate `lastmod` dates when supplied. Canonical annotations, redirects and sitemap inclusion are complementary signals; Google still chooses its own canonical. See Google's current [canonical URL guidance](https://developers.google.com/search/docs/crawling-indexing/consolidate-duplicate-urls).

## 2. Verify Google Search Console

GitHub Pages under `/MinkowskiKart/` cannot use a project-specific DNS record, so create a **URL-prefix** property:

1. Sign in at [Google Search Console](https://search.google.com/search-console/).
2. Open the property selector and choose **Add property**.
3. Under **URL prefix**, enter `https://rchristie95.github.io/MinkowskiKart/`, including HTTPS and the final slash.
4. Choose **HTML tag** verification and copy the complete verification `<meta>` element.
5. Add that element inside the `<head>` of the deployed homepage source, commit and push it, and wait for Pages to deploy.
6. View the public homepage source and confirm the exact tag is present.
7. Return to Search Console and select **Verify**. Leave the tag in place after verification.

Google documents the distinction between Domain and URL-prefix properties in [Add a website property](https://support.google.com/webmasters/answer/34592). If an existing verified owner already controls the precise URL-prefix property, request access instead of creating conflicting setup.

## 3. Submit the sitemap to Google

1. Select the verified Minkowski Kart property.
2. Open **Sitemaps**.
3. In **Add a new sitemap**, enter `https://rchristie95.github.io/MinkowskiKart/sitemap.xml` and select **Submit**.
4. Confirm that the status becomes **Success** and that the discovered URL count is plausible for this small site.
5. If it fails, inspect the sitemap URL itself, fix the reported fetch or XML error, deploy, and resubmit.

This follows Google's current [Sitemaps report instructions](https://support.google.com/webmasters/answer/7451001).

## 4. Request Google indexing

For both the homepage and download page:

1. Paste the complete canonical URL into the **URL Inspection** bar.
2. Select **Test live URL**.
3. Confirm that the fetch succeeds, indexing is allowed and the rendered page contains its primary content.
4. Select **Request indexing** once. Repeated requests do not speed up crawling.

Repeat live inspection for the Physics, About and FAQ pages if the sitemap reports a problem. For large or routine changes, update accurate sitemap `lastmod` values instead of manually requesting every page. See Google's [URL Inspection documentation](https://support.google.com/webmasters/answer/9012289).

## 5. Verify Bing Webmaster Tools

The simplest route is to import the verified Google property:

1. Sign in at [Bing Webmaster Tools](https://www.bing.com/webmasters/).
2. Choose **Import from Google Search Console**.
3. Grant Bing the requested read access, select only the Minkowski Kart property, and choose **Import**.
4. Confirm that Bing shows the correct URL and imported sitemap.

Bing documents that Search Console imports transfer the selected verified property and its sitemaps. Review the current [Add and Verify Site instructions](https://www.bing.com/webmasters/help/add-and-verify-site-12184f8b) before granting access.

If import is unavailable, choose **Add site**, enter the exact property URL, and use Bing's HTML meta-tag method. Put the supplied tag in the deployed homepage `<head>`, verify it in public source, then complete verification. Keep the verification tag in place. Do not paste a token from another site.

## 6. Submit the sitemap and pages to Bing

1. Select the verified site in Bing Webmaster Tools.
2. Open **Sitemaps**, choose **Submit sitemaps**, enter `https://rchristie95.github.io/MinkowskiKart/sitemap.xml`, and submit it.
3. Check the processed status and discovered URL count. Bing's [Sitemaps documentation](https://www.bing.com/webmasters/help/sitemaps-3b5cf6ed) explains the report and error details.
4. Open **URL Inspection**, enter the homepage, and use **Live URL** to confirm what Bingbot receives.
5. If it is crawlable and not indexed, choose **Request indexing**.
6. Repeat for `https://rchristie95.github.io/MinkowskiKart/download.html`.

Alternatively, open **Submit URLs**, enter the two canonical URLs on separate lines, and submit them once. Bing notes that manual submission does not guarantee inclusion and repeated submissions do not accelerate it; see [URL Submission](https://www.bing.com/webmasters/help/URL-Submission-62f2860b).

## 7. Check canonical selection

In Google URL Inspection, expand **Page indexing** for each indexed page and compare **User-declared canonical** with **Google-selected canonical**. Both should be the same HTTPS project URL. If not:

- remove duplicate content at alternate URLs or redirect it where possible;
- ensure all internal links use the canonical URL;
- ensure only canonical URLs appear in the sitemap;
- keep the self-referencing canonical tag in the initial HTML source;
- inspect the chosen alternate to discover why Google prefers it.

In Bing, use **URL Inspection** and its Index, SEO and Markup cards, then review **Site Explorer** for redirects, canonical sources and excluded URLs. Bing's [URL Inspection guide](https://www.bing.com/webmasters/help/URL-Inspection-55a30305) describes those reports.

## 8. Check mobile usability

Google recommends responsive design and uses the mobile version for indexing. For the homepage and download page:

1. Open Chrome DevTools, select a mobile viewport, and run **Lighthouse** in Navigation mode with **Device: Mobile** and Performance, Accessibility, Best Practices and SEO enabled.
2. Manually test at 320, 375 and 768 CSS pixels wide. Verify readable text, no horizontal scrolling, usable navigation and tap targets, visible focus, complete content and correctly sized images.
3. Repeat with JavaScript disabled; the core project and download information must remain available.
4. Test the public URLs with Bing's [Mobile Friendliness Test](https://www.bing.com/webmaster/tools/mobile-friendliness).

Record the date and exported Lighthouse reports. Lighthouse is a diagnostic aid, not a guarantee of search ranking; see the official [Lighthouse documentation](https://developer.chrome.com/docs/lighthouse/) and Google's [mobile-first indexing guidance](https://developers.google.com/search/docs/crawling-indexing/mobile/mobile-sites-mobile-first-indexing).

## 9. Check structured data and rich-result eligibility

1. Open Google's [Rich Results Test](https://search.google.com/test/rich-results).
2. Test the deployed homepage URL, not a pasted development snippet.
3. Fix every critical error. Review warnings and add a field only when it is visible on the page and verified.
4. Validate the same JSON-LD with the [Schema.org validator](https://validator.schema.org/).
5. In Google URL Inspection, inspect the live homepage and confirm the enhancement is detected.
6. In Bing URL Inspection, review the **Markup** card for its JSON-LD and Open Graph interpretation.

For Google's `SoftwareApplication` result, `name` and `offers.price` are required; `applicationCategory` and `operatingSystem` are recommended. Use `GameApplication`, a zero-price offer, `version_1.9`, and list `Windows, macOS, Linux, Android` in `operatingSystem`, matching the visible supported-platform copy. Do not list iOS while it remains developer/ad-hoc. Never add ratings, review counts or performance claims that the project cannot substantiate. Google's current requirements are in its [`SoftwareApplication` structured-data documentation](https://developers.google.com/search/docs/appearance/structured-data/software-app).

A valid test makes the page eligible for the relevant treatment; it does not guarantee a rich result or indexing.

## 10. Record completion

Add a dated entry to the release checklist with:

- verification method and property URL for each service;
- sitemap submission status and discovered URL count;
- inspection status for homepage and download page;
- declared and selected canonicals;
- Lighthouse report filenames and material findings;
- Rich Results Test and Schema.org validation results;
- unresolved warnings or account-access blockers.

Never commit account cookies, API keys or access tokens. Search verification meta tags are designed to be public, but use only tokens generated for this project and leave account recovery material out of the repository.
