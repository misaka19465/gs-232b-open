/*
 * GS-232B Open - rotator controller firmware.
 * Copyright (C) 2026 BG5GEB <bg5geb@misakanet.team>
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

import { configureLocalization, msg } from '@lit/localize';
import source from './messages.zh-CN.js';
import english from './messages.en.js';

const { getLocale, setLocale } = configureLocalization({
  sourceLocale: 'zh-CN',
  targetLocales: ['en'],
  // NOTE: 两种语言均内联进离线 HTML，不从设备热点外加载翻译资源。
  loadLocale: async () => ({ templates: english }),
});
const messageValues = new WeakMap();

/**
 * 返回当前语言的消息，动态参数由源语言模板定义顺序。
 * @param {string} key 消息目录中的键。
 * @param {object} [values] 动态模板参数。
 * @returns {string} 本地化纯文本。
 */
export function t(key, values) {
  const template = typeof source[key] === 'function' ? source[key](values) : source[key];
  return msg(template, { id: key });
}

/**
 * 绑定当前消息和参数，语言变化时可原地更新而不重建交互控件。
 * @param {HTMLElement} element 文本容器。
 * @param {string} key 消息目录中的键。
 * @param {object} [values] 动态模板参数。
 */
export function setMessage(element, key, values) {
  element.dataset.text = key;
  messageValues.set(element, values);
  element.textContent = t(key, values);
}

/** 更新所有声明式消息，包括动态文本、字段标签、帮助文本和无障碍名称。 */
export function renderMessages() {
  for (const element of document.querySelectorAll('[data-text]')) {
    element.textContent = t(element.dataset.text, messageValues.get(element));
  }
  for (const [data, attribute] of [
    ['label', 'label'], ['helper', 'helper'], ['aria', 'aria-label'], ['description', 'description'],
  ]) {
    for (const element of document.querySelectorAll(`[data-${data}]`)) {
      element.setAttribute(attribute, t(element.dataset[data]));
    }
  }
  document.title = `GS-232B · ${t('title')}`;
}

function browserLocale() {
  const languages = navigator.languages?.length ? navigator.languages : [navigator.language];
  for (const language of languages) {
    const base = language.toLowerCase().split('-')[0];
    if (base === 'zh') return 'zh-CN';
    if (base === 'en') return 'en';
  }
  return 'en';
}

/** 初始化浏览器首选语言，并在语言偏好变化时同步页面和 HTML 语言标记。 */
export function followBrowserLanguage() {
  const update = async () => {
    await setLocale(browserLocale());
    document.documentElement.lang = getLocale();
    renderMessages();
  };
  window.addEventListener('languagechange', update);
  update();
}
