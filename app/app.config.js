const allowLanHttp = process.env.EXPO_PUBLIC_ALLOW_LAN_HTTP === '1';
const projectId = process.env.EXPO_PUBLIC_EAS_PROJECT_ID;
const validProjectId = /^[a-f\d]{8}(-[a-f\d]{4}){3}-[a-f\d]{12}$/i.test(projectId || '');

module.exports = {
  expo: {
    name: 'Portão Inteligente',
    slug: 'portao-inteligente',
    version: '2.0.0',
    orientation: 'portrait',
    scheme: 'portaointeligente',
    userInterfaceStyle: 'light',
    android: {
      package: 'com.seunome.portaointeligente',
      // Arquivo de configuracao do cliente Firebase. A conta de servico e enviada
      // ao EAS Credentials e NUNCA deve ser incluida no app.
      ...(process.env.GOOGLE_SERVICES_JSON ? { googleServicesFile: process.env.GOOGLE_SERVICES_JSON } : {}),
    },
    ios: {
      bundleIdentifier: 'com.seunome.portaointeligente',
      supportsTablet: true,
      infoPlist: {
        NSLocalNetworkUsageDescription: 'Conectar ao servidor do portão na rede de teste.',
        ...(allowLanHttp ? { NSAppTransportSecurity: { NSAllowsArbitraryLoads: true } } : {}),
      },
    },
    plugins: [
      'expo-dev-client',
      ['expo-notifications', { defaultChannel: 'campainha', color: '#00856A' }],
      'expo-secure-store',
      ['./plugins/withLanHttp', { enabled: allowLanHttp }],
    ],
    extra: { ...(validProjectId ? { eas: { projectId } } : {}) },
  },
};
