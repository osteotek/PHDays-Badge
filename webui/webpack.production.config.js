const webpack = require('webpack');
const { CleanWebpackPlugin } = require('clean-webpack-plugin');
const CopyWebpackPlugin = require('copy-webpack-plugin');
const HtmlWebpackPlugin = require('html-webpack-plugin');
const MiniCssExtractPlugin = require('mini-css-extract-plugin');
const Dotenv = require('dotenv-webpack');
const TerserPlugin = require("terser-webpack-plugin");
const CssMinimizerPlugin = require("css-minimizer-webpack-plugin");
const CompressionPlugin = require("compression-webpack-plugin");
const path = require('path');

const config = {
  mode: "production",
  entry: [
    './src/utils/polyfills.js',
    './src/index.jsx',
  ],
  output: {
    // filename: 'bundle.js',
    filename: '[name].js',
    path: path.join(__dirname, '/deploy'),
    clean: true,
  },
  optimization: {
    minimize: true,
    minimizer: [
      new TerserPlugin({
        terserOptions: {
          compress: true,
          sourceMap: true,
          // parallel: true,
          compress: {
            drop_console: true,
          },
          mangle: true,
        },
      }),
      new CssMinimizerPlugin(),
    ],
    concatenateModules: true,
    chunkIds: 'total-size',
    moduleIds: 'size',
    innerGraph: true,
    mangleExports: true,
    mangleWasmImports: true,
    mergeDuplicateChunks: true,
    splitChunks: {
      chunks: "all",
      // minSize: 20000,
      minSize: 0,
      maxAsyncRequests: 1,
      maxInitialRequests: 1,
      // cacheGroups: {
      //   // bootstrap: {
      //   //   filename: '[name].[chunkhash].js'
      //   // },
      //   styles: {
      //     name: 'styles',
      //     chunks: 'all',
      //     enforce: true,
      //     test: /\.s?css$/
      // }
      // }
    }
  },
  module: {
    rules: [
      {
        test: /\.jsx?$/,
        exclude: /node_modules/,
        loader: 'babel-loader'
      },
      {
        test: /\.css$/i,
        use: [
          MiniCssExtractPlugin.loader,
          'css-loader',
          'postcss-loader',
        ],
      },
      // {
      //   test: /\.(ttf|eot|svg|woff(2)?)(\?v=[\d.]+)?(\?[a-z0-9#-]+)?$/,
      //   loader: 'url-loader',
      //   options: {
      //     limit: 1000000,
      //     name: '[hash].[ext]',
      //   },
      // }
      {
        test: /\.(ttf|eot|svg|woff(2)?)(\?v=[\d.]+)?(\?[a-z0-9#-]+)?$/,
        type: 'asset',   // <-- Assets module - asset
        parser: {
          dataUrlCondition: {
            maxSize: 8 * 1024 // 8kb
          }
        },
        generator: {  //If emitting file, the file path is
          // filename: '[hash][ext][query]'
          filename: '[name][ext]'
        }
      }
    ]
  },
  resolve: {
    extensions: ['.js', '.jsx'],
    fallback: {
      stream: require.resolve("stream-browserify"),
      util: require.resolve("util"),
      buffer: require.resolve("buffer/"),
      assert: require.resolve("assert/"),
      'process/browser': require.resolve('process/browser'),
    },
  },
  plugins: [
    new Dotenv(),
    new webpack.DefinePlugin({
      'process.env.NODE_ENV': '"production"'
    }),
    new CleanWebpackPlugin(),
    new CopyWebpackPlugin({
      patterns: [
        { from: 'src/assets/favicon.ico', to: 'favicon.ico' },
        { from: 'src/assets/apple-touch-icon.png', to: 'apple-touch-icon.png' },
        // { from: 'src/assets/regular-icon.png', to: 'regular-icon.png' },
        // { from: 'src/assets/coindrop-img.png', to: 'coindrop-img.png' },
        { from: './public/_redirects', to: './' }
      ],
    }),
    new MiniCssExtractPlugin({
      filename: '[name].css',
      chunkFilename: '[name]-[id].css',
    }),
    new HtmlWebpackPlugin({
      template: './public/index.html',
      inject: true,
      // The firmware serves main.js/main.css with a one-day cache; a per-build
      // query string makes browsers fetch new builds.
      hash: true,
    }),
    new webpack.ProvidePlugin({
      process: 'process/browser',
      Buffer: ['buffer', 'Buffer'],
    }),
    new CompressionPlugin({
      filename: "[path][base].gz",
      algorithm: "gzip",
      compressionOptions: { level: 9 },
      test: /\.js$|\.css$|\.(woff(2)?|eot|ttf|otf|svg|)$/,
      threshold: 8096,
      minRatio: 0.9,
      deleteOriginalAssets: true,
    })
  ],
  target: "web",
  stats: false,
};

module.exports = config;
