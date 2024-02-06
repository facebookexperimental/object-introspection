/**
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 * @format
 */
// @ts-check
// Note: type annotations allow type checking and IDEs autocompletion

/** @type {import('@docusaurus/types').Config} */
const config = {
  title: 'Object Introspection',
  tagline: 'Dynamic C++ Object Profiling',
  url: 'https://objectintrospection.org',
  baseUrl: '/',
  onBrokenLinks: 'throw',
  onBrokenMarkdownLinks: 'warn',
  favicon: '/img/favicon.ico',
	trailingSlash: false,

  // GitHub pages deployment config.
  // If you aren't using GitHub pages, you don't need these.
  organizationName: 'facebookexperimental', // Usually your GitHub org/user name.
  projectName: 'object-introspection', // Usually your repo name.
	deploymentBranch: 'gh-pages',

  presets: [
    [
      'classic',
      /** @type {import('@docusaurus/preset-classic').Options} */
      ({
        docs: {
          sidebarPath: require.resolve('./sidebars.js'),
          // Please change this to your repo.
          // Remove this to remove the "edit this page" links.
          editUrl:
            'https://github.com/facebookexperimental/object-introspection',
        },
        theme: {
          customCss: require.resolve('./src/css/custom.css'),
        },
      }),
    ],
  ],

  headTags: [
    // Favicons declarations
    { tagName: 'link', attributes: { rel: 'apple-touch-icon', sizes: '180x180', href: '/img/apple-touch-icon.png' } },
    { tagName: 'link', attributes: { rel: 'icon', sizes: '32x32', href: 'img/favicon-32x32.png' } },
    { tagName: 'link', attributes: { rel: 'icon', sizes: '16x16', href: 'img/favicon-16x16.png' } },
    { tagName: 'link', attributes: { rel: 'manifest', href: '/site.webmanifest' } },
    { tagName: 'link', attributes: { rel: 'mask-icon', href: '/safari-pinned-tab.svg', color: '#5bbad5' } },
    { tagName: 'meta', attributes: { name: 'apple-mobile-web-app-title', content: 'Object Introspection' } },
    { tagName: 'meta', attributes: { name: 'application-name', content: 'Object Introspection' } },
    { tagName: 'meta', attributes: { name: 'msapplication-TileColor', content: '#da532c' } },
    { tagName: 'meta', attributes: { name: 'theme-color', content: '#ffffff' } }
  ],

  themeConfig:
    /** @type {import('@docusaurus/preset-classic').ThemeConfig} */
    ({
      navbar: {
        logo: {
          alt: 'OI Logo',
          src: 'img/OIBrandmark.svg',
        },
        items: [
          // Please keep GitHub link to the right for consistency.
          {
						href: 'https://github.com/facebookexperimental/object-introspection',
            label: 'GitHub',
            position: 'right',
          },
        ],
      },
      footer: {
        style: 'dark',
        links: [
          {
            title: 'Links',
            items: [
              {
                label: 'Getting Started',
                to: 'docs/intro',
              },
              {
                label: 'CppCon 2023 Presentation',
                to: 'https://youtu.be/6IlTs8YRne0',
              },
            ],
          },
          {
            title: 'Community',
            items: [
              {
                label: 'Matrix',
                href: 'https://matrix.to/#/#object-introspection:matrix.org',
              },
              {
                label: 'IRC',
                href: 'irc://irc.oftc.net/#object-introspection',
              },
            ],
          },
          {
            title: 'Code',
            items: [
              {
                label: 'GitHub',
                href: 'https://github.com/facebookexperimental/object-introspection',
              },
            ],
          },
          {
            title: 'Legal',
            // Please do not remove the privacy and terms, it's a legal requirement.
            items: [
              {
                label: 'Privacy',
                href: 'https://opensource.fb.com/legal/privacy/',
              },
              {
                label: 'Terms',
                href: 'https://opensource.fb.com/legal/terms/',
              },
            ],
          },
        ],
        logo: {
          alt: 'Meta Open Source Logo',
          // This default includes a positive & negative version, allowing for
          // appropriate use depending on your site's style.
          src: '/img/meta_opensource_logo_negative.svg',
          href: 'https://opensource.fb.com',
        },
        // Please do not remove the credits, help to publicize Docusaurus :)
        copyright: `Copyright © ${new Date().getFullYear()} Meta Platforms, Inc. Built with Docusaurus.`,
      },
    }),
};

module.exports = config;
