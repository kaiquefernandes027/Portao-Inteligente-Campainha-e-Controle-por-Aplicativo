const { withAndroidManifest } = require('expo/config-plugins');
module.exports = (config, { enabled = false } = {}) => withAndroidManifest(config, (mod) => {
  const application = mod.modResults.manifest.application?.[0];
  if (application) application.$['android:usesCleartextTraffic'] = enabled ? 'true' : 'false';
  return mod;
});
