# PHDays Badge WebUI

## Installation

```bash
npm install
```

## Development

```bash
npm run development
```

## Build

Create the production build.

```bash
npm run build --verbose
```

## Lint

There are several libraries used in the project that help us to keep our codebase healthy:

- [ESlint](https://eslint.org/)
- [Stylelint](https://stylelint.io/)
- [Prettier](https://prettier.io/)

Every time we commit something it will execute the linters and format the staged files if needed.

If you want to check them individually you could execute the following scripts:

```bash
npm run lint
npm run csslint
npm run format
```


Huge thanks to [@jvalen](https://github.com/jvalen) for the [pixel-art-react](https://github.com/jvalen/pixel-art-react) repository.