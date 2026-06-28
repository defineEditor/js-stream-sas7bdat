import path from 'path';
import { DatasetSpss, DatasetStata } from '../src/index';

const sampleDTA = path.join(__dirname, 'data', 'sample.dta');
const sampleSAV = path.join(__dirname, 'data', 'sample.sav');
const sampleZSAV = path.join(__dirname, 'data', 'sample.zsav');
const samplePOR = path.join(__dirname, 'data', 'sample.por');

describe('DatasetStata', () => {
    test('reads metadata for sample.dta', async () => {
        const data = new DatasetStata(sampleDTA);

        await expect(data.getMetadata()).resolves.toMatchObject({
            records: 5,
            name: 'sample',
            label: '',
            columns: [
                {
                    itemOID: 'IT.mychar',
                    name: 'mychar',
                    dataType: 'string',
                    displayFormat: '%-1s',
                },
                {
                    itemOID: 'IT.mynum',
                    name: 'mynum',
                    dataType: 'double',
                    displayFormat: '%16.2f',
                },
                {
                    itemOID: 'IT.mydate',
                    name: 'mydate',
                    dataType: 'double',
                    displayFormat: '%td',
                },
                {
                    itemOID: 'IT.dtime',
                    name: 'dtime',
                    dataType: 'double',
                    displayFormat: '%tc',
                },
                {
                    itemOID: 'IT.mylabl',
                    name: 'mylabl',
                    dataType: 'integer',
                    displayFormat: '%16.0f',
                },
                {
                    itemOID: 'IT.myord',
                    name: 'myord',
                    dataType: 'integer',
                    displayFormat: '%16.0f',
                },
                {
                    itemOID: 'IT.mytime',
                    name: 'mytime',
                    dataType: 'double',
                    displayFormat: '%tcHH:MM:SS',
                },
            ],
        });
    });

    test('reads sample.dta rows and chunked subsets', async () => {
        const data = new DatasetStata(sampleDTA);

        await expect(
            data.getData({
                start: 0,
                length: 5,
            }),
        ).resolves.toEqual({
            data: [
                ['a', 1.1, 21310, 1841220610000, 1, 1, 36610000],
                ['b', 1.2, -29093, -2513598590000, 2, 2, 83410000],
                ['c', -1000.3, 0, 0, 1, 3, 0],
                ['d', -1.4, -137696, -11896934400000, 2, 1, 58210000],
                ['e', 1000.3, null, null, 1, 1, null],
            ],
            lastRow: 4,
            endReached: true,
        });

        await expect(
            data.getData({
                start: 1,
                length: 2,
                filterColumns: ['myord', 'mynum'],
            }),
        ).resolves.toEqual({
            data: [
                [2, 1.2],
                [3, -1000.3],
            ],
            lastRow: 2,
            endReached: false,
        });
    });
});

describe('DatasetSpss', () => {
    test('reads metadata for sample.sav', async () => {
        const data = new DatasetSpss(sampleSAV);

        await expect(data.getMetadata()).resolves.toMatchObject({
            records: 5,
            name: 'sample',
            label: '',
            columns: [
                {
                    itemOID: 'IT.mychar',
                    name: 'mychar',
                    dataType: 'string',
                    displayFormat: 'A1',
                },
                {
                    itemOID: 'IT.mynum',
                    name: 'mynum',
                    dataType: 'double',
                    displayFormat: 'F8.2',
                },
                {
                    itemOID: 'IT.mydate',
                    name: 'mydate',
                    dataType: 'double',
                    displayFormat: 'EDATE10',
                },
                {
                    itemOID: 'IT.dtime',
                    name: 'dtime',
                    dataType: 'double',
                    displayFormat: 'DATETIME20',
                },
                {
                    itemOID: 'IT.mylabl',
                    name: 'mylabl',
                    dataType: 'double',
                    displayFormat: 'F8.2',
                },
                {
                    itemOID: 'IT.myord',
                    name: 'myord',
                    dataType: 'double',
                    displayFormat: 'F8.2',
                },
                {
                    itemOID: 'IT.mytime',
                    name: 'mytime',
                    dataType: 'double',
                    displayFormat: 'TIME8',
                },
            ],
        });
    });

    test('reads sample.sav rows and chunked subsets', async () => {
        const data = new DatasetSpss(sampleSAV);

        await expect(
            data.getData({
                start: 0,
                length: 5,
            }),
        ).resolves.toEqual({
            data: [
                ['a', 1.1, 13744944000, 13744980610, 1, 1, 36610],
                ['b', 1.2, 9390124800, 9390161410, 2, 2, 83410],
                ['c', -1000.3, 11903760000, 11903760000, 1, 3, 0],
                ['d', -1.4, 6825600, 6825600, 2, 1, 58210],
                ['e', 1000.3, null, null, 1, 1, null],
            ],
            lastRow: 4,
            endReached: true,
        });

        await expect(
            data.getData({
                start: 1,
                length: 2,
                filterColumns: ['myord', 'mynum'],
            }),
        ).resolves.toEqual({
            data: [
                [2, 1.2],
                [3, -1000.3],
            ],
            lastRow: 2,
            endReached: false,
        });
    });

    test('reads metadata for sample.zsav', async () => {
        const data = new DatasetSpss(sampleZSAV);

        await expect(data.getMetadata()).resolves.toMatchObject({
            records: 5,
            name: 'sample',
            label: '',
            columns: [
                {
                    itemOID: 'IT.mychar',
                    name: 'mychar',
                    dataType: 'string',
                    displayFormat: 'A1',
                },
                {
                    itemOID: 'IT.mynum',
                    name: 'mynum',
                    dataType: 'double',
                    displayFormat: 'F8.2',
                },
                {
                    itemOID: 'IT.mydate',
                    name: 'mydate',
                    dataType: 'double',
                    displayFormat: 'EDATE10',
                },
                {
                    itemOID: 'IT.dtime',
                    name: 'dtime',
                    dataType: 'double',
                    displayFormat: 'DATETIME20',
                },
                {
                    itemOID: 'IT.mylabl',
                    name: 'mylabl',
                    dataType: 'double',
                    displayFormat: 'F8.2',
                },
                {
                    itemOID: 'IT.myord',
                    name: 'myord',
                    dataType: 'double',
                    displayFormat: 'F8.2',
                },
                {
                    itemOID: 'IT.mytime',
                    name: 'mytime',
                    dataType: 'double',
                    displayFormat: 'TIME8',
                },
            ],
        });
    });

    test('reads sample.zsav rows and chunked subsets', async () => {
        const data = new DatasetSpss(sampleZSAV);

        await expect(
            data.getData({
                start: 0,
                length: 5,
            }),
        ).resolves.toEqual({
            data: [
                ['a', 1.1, 13744944000, 13744980610, 1, 1, 36610],
                ['b', 1.2, 9390124800, 9390161410, 2, 2, 83410],
                ['c', -1000.3, 11903760000, 11903760000, 1, 3, 0],
                ['d', -1.4, 6825600, 6825600, 2, 1, 58210],
                ['e', 1000.3, null, null, 1, 1, null],
            ],
            lastRow: 4,
            endReached: true,
        });

        await expect(
            data.getData({
                start: 1,
                length: 2,
                filterColumns: ['myord', 'mynum'],
            }),
        ).resolves.toEqual({
            data: [
                [2, 1.2],
                [3, -1000.3],
            ],
            lastRow: 2,
            endReached: false,
        });
    });

    test('reads metadata for sample.por', async () => {
        const data = new DatasetSpss(samplePOR);

        await expect(data.getMetadata()).resolves.toMatchObject({
            records: 5,
            name: 'sample',
            label: '',
            columns: [
                {
                    itemOID: 'IT.MYCHAR',
                    name: 'MYCHAR',
                    dataType: 'string',
                    displayFormat: 'A1',
                },
                {
                    itemOID: 'IT.MYNUM',
                    name: 'MYNUM',
                    dataType: 'double',
                    displayFormat: 'F8.2',
                },
                {
                    itemOID: 'IT.MYDATE',
                    name: 'MYDATE',
                    dataType: 'double',
                    displayFormat: 'EDATE10',
                },
                {
                    itemOID: 'IT.DTIME',
                    name: 'DTIME',
                    dataType: 'double',
                    displayFormat: 'DATETIME20',
                },
                {
                    itemOID: 'IT.MYLABL',
                    name: 'MYLABL',
                    dataType: 'double',
                    displayFormat: 'F8.2',
                },
                {
                    itemOID: 'IT.MYORD',
                    name: 'MYORD',
                    dataType: 'double',
                    displayFormat: 'F8.2',
                },
                {
                    itemOID: 'IT.MYTIME',
                    name: 'MYTIME',
                    dataType: 'double',
                    displayFormat: 'TIME8',
                },
            ],
        });
    });

    test('reads sample.por rows and chunked subsets', async () => {
        const data = new DatasetSpss(samplePOR);

        await expect(
            data.getData({
                start: 0,
                length: 5,
            }),
        ).resolves.toEqual({
            data: [
                ['a', 1.1, 13744944000, 13744980610, 1, 1, 36610],
                ['b', 1.2, 9390124800, 9390161410, 2, 2, 83410],
                ['c', -1000.3, 11903760000, 11903760000, 1, 3, 0],
                ['d', -1.4, 6825600, 6825600, 2, 1, 58210],
                ['e', 1000.3, null, null, 1, 1, null],
            ],
            lastRow: 4,
            endReached: true,
        });

        await expect(
            data.getData({
                start: 1,
                length: 2,
                filterColumns: ['MYORD', 'MYNUM'],
            }),
        ).resolves.toEqual({
            data: [
                [2, 1.2],
                [3, -1000.3],
            ],
            lastRow: 2,
            endReached: false,
        });
    });
});