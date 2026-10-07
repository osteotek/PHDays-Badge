const webpack = require('webpack');
const CopyWebpackPlugin = require('copy-webpack-plugin');
const HtmlWebpackPlugin = require('html-webpack-plugin');
const MiniCssExtractPlugin = require('mini-css-extract-plugin');
const CssMinimizerPlugin = require("css-minimizer-webpack-plugin");
const TerserPlugin = require("terser-webpack-plugin");
const CompressionPlugin = require("compression-webpack-plugin");
const Dotenv = require('dotenv-webpack');
const path = require('path');

module.exports = {
  mode: "development",
  devtool: 'cheap-module-source-map',            
  entry: [
    './src/utils/polyfills.js',
    './src/index.jsx',
  ],
  output: {
    // filename: 'bundle.js',
    filename: '[name].bundle.js',
    path: path.join(__dirname, '/public'),
  },
    // optimization: {
    //   minimize: true,
    //   minimizer: [
    //     new TerserPlugin({
    //       terserOptions: {
    //         compress: true,
    //         sourceMap: true,
    //         // parallel: true,
    //         compress: {
    //           drop_console: true,
    //         },
    //         mangle: true,
    //       },
    //     }),
    //     new CssMinimizerPlugin(),
    //   ],
    //   concatenateModules: true,
    //   chunkIds: 'total-size',
    //   moduleIds: 'size',
    //   innerGraph: true,
    //   mangleExports: true,
    //   mangleWasmImports: true,
    //   mergeDuplicateChunks: true,
    //   splitChunks: {
    //     chunks: "all",
    //     // minSize: 20000,
    //     minSize: 0,
    //     maxAsyncRequests: 10,
    //     maxInitialRequests: 10,
    //     // cacheGroups: {
    //     //   // bootstrap: {
    //     //   //   filename: '[name].[chunkhash].js'
    //     //   // },
    //     //   styles: {
    //     //     name: 'styles',
    //     //     chunks: 'all',
    //     //     enforce: true,
    //     //     test: /\.s?css$/
    //     // }
    //     // }
    //   }
    // },
  module: {
    rules: [
      {
        test: /\.jsx?$/,
        exclude: /node_modules/,
        use: [
          'babel-loader'
        ]
      },
      {
        test:   /\.css$/i,
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
      'process.env.NODE_ENV': '"development"'
    }),
    new CopyWebpackPlugin({
      patterns: [
        { from: 'src/assets/favicon.ico', to: 'favicon.ico' },
        // { from: 'src/assets/coindrop-img.png', to: 'coindrop-img.png' }
      ],
    }),
    new MiniCssExtractPlugin({
      filename: '[name].css',
      chunkFilename: '[name]-[id].css',
    }),
    new HtmlWebpackPlugin({
      template: './public/index.dev.html',
      inject: true,
    }),
    new webpack.ProvidePlugin({
      process: 'process/browser',
      Buffer: ['buffer', 'Buffer'],
    }),
  ],
  devServer: {
    static: './public',
    historyApiFallback: true,
  },
  target: "web",
  stats: false
};
