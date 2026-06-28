import path from 'path';
const projectRoot = path.resolve(__dirname, '..');
// eslint-disable-next-line @typescript-eslint/no-require-imports
const binding = require('node-gyp-build')(projectRoot);
import {
    createDatasetReader,
    DatasetReadStat,
    DatasetSas7BDat,
    DatasetStata,
    DatasetSpss,
} from '../src/index';

test('Factory returns SAS reader for sas7bdat files', async () => {
    const filePath = path.join(__dirname, 'data', 'sample.sas7bdat');

    const data = createDatasetReader(filePath);

    expect(data).toBeInstanceOf(DatasetSas7BDat);
    await expect(data.getMetadata()).resolves.toMatchObject({
        records: 19,
    });
});

test('Native binding exposes separate format readers', () => {
    expect(typeof binding.readDta).toEqual('function');
    expect(typeof binding.readDtaAsync).toEqual('function');
    expect(typeof binding.readDtaStream).toEqual('function');
    expect(typeof binding.getDtaMetadata).toEqual('function');

    expect(typeof binding.readSav).toEqual('function');
    expect(typeof binding.readSavAsync).toEqual('function');
    expect(typeof binding.readSavStream).toEqual('function');
    expect(typeof binding.getSavMetadata).toEqual('function');

    expect(typeof binding.readPor).toEqual('function');
    expect(typeof binding.readPorAsync).toEqual('function');
    expect(typeof binding.readPorStream).toEqual('function');
    expect(typeof binding.getPorMetadata).toEqual('function');
});

test('Generic ReadStat reader can read sas7bdat files', async () => {
    const filePath = path.join(__dirname, 'data', 'sample.sas7bdat');
    const genericReader = new DatasetReadStat(filePath);
    const sasReader = new DatasetSas7BDat(filePath);

    await expect(
        genericReader.getData({
            start: 0,
            length: 2,
            filterColumns: ['NAME', 'SEX'],
        }),
    ).resolves.toEqual(
        await sasReader.getData({
            start: 0,
            length: 2,
            filterColumns: ['NAME', 'SEX'],
        }),
    );
});

test('Generic ReadStat reader can read DTA files', async () => {
    const filePath = path.join(__dirname, 'data', 'sample.dta');
    const genericReader = new DatasetReadStat(filePath);
    const dtaReader = new DatasetStata(filePath);

    const genericMetadata = await genericReader.getMetadata();
    const dtaMetadata = await dtaReader.getMetadata();

    const genericData = await genericReader.getData({
        start: 0,
    });
    const dtaData = await dtaReader.getData({
        start: 0,
    });

    expect(genericMetadata).toEqual(dtaMetadata);
    expect(genericData).toEqual(dtaData);
});

test('Factory returns Stata reader for dta files', () => {
    const data = createDatasetReader('/tmp/example.dta');

    expect(data).toBeInstanceOf(DatasetStata);
});

test('Factory returns SPSS reader for sav files', () => {
    const data = createDatasetReader('/tmp/example.sav');

    expect(data).toBeInstanceOf(DatasetSpss);
});

test('Factory returns SPSS reader for por files', () => {
    const data = createDatasetReader('/tmp/example.por');

    expect(data).toBeInstanceOf(DatasetSpss);
});
