// Karma configuration
// Generated on Wed Apr 17 2019 10:00:31 GMT+0200 (Central European Summer Time)

module.exports = function(config) {
  config.set({
    frameworks: ["jasmine", "commonjs"],
    files: ["test/**/*.ts", "src/**/*"],
    exclude: ["src/**/*.d.ts"],
    preprocessors: {
      "**/*.ts": ["typescript", "commonjs"],
      "src/**/*.js": ["typescript", "commonjs"]
    },
    karmaTypescriptConfig: {
      compilerOptions: {
        lib: ["DOM", "es6"]
      },
    },
    reporters: ["progress"],
    browsers: ["ChromeHeadless"],
    //logLevel: config.LOG_DEBUG,
    singleRun: true
  });
};
