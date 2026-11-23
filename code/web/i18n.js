import en from "./locales/en.js";
import zh from "./locales/zh-CN.js";

export const locales = Object.freeze({en, "zh-CN": zh});
export function translator(locale) {
  const dictionary = locales[locale];
  if (!dictionary) throw new Error(`Unknown locale: ${locale}`);
  return key => dictionary[key] ?? en[key] ?? key;
}
export function displayName(record, locale, fallback) {
  return record?.[`label_${locale === "zh-CN" ? "zh" : "en"}`] ?? record?.label ?? fallback;
}
