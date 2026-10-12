import type { CapacitorConfig } from '@capacitor/cli';

const config: CapacitorConfig = {
  appId: 'app.aura.network',
  appName: 'Aura',
  webDir: 'dist',
  backgroundColor: '#09080e',
  ios: {
    contentInset: 'never',
    backgroundColor: '#09080e',
  },
  android: {
    backgroundColor: '#09080e',
  },
  plugins: {
    SplashScreen: {
      launchAutoHide: false,
      backgroundColor: '#09080e',
      showSpinner: false,
    },
    Keyboard: {
      resize: 'body',
      style: 'DARK',
    },
    StatusBar: {
      style: 'DARK',
      overlaysWebView: true,
    },
  },
};

export default config;
