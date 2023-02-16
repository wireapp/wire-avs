// Global variable initialization
buildNumber = currentBuild.id
version = null
release_version = ""
branchName = ""
commitId = null
repoName = null
changelog = ""

pipeline {
    agent none

    options {
        parallelsAlwaysFailFast()
        disableConcurrentBuilds()
    }

    // NOTE: checks every 5 minutes if a new commit occurred after last successful run
    triggers {
        pollSCM 'H/5 * * * *'
    }

    stages {
       stage('Test + Build') {
            parallel {
                stage('Linux') {
                    agent {
                        dockerfile true
                    }
                    steps {
                        script {
                            def vcs = checkout([
                                    $class: 'GitSCM',
                                    changelog: true,
                                    userRemoteConfigs: scm.userRemoteConfigs,
                                    branches: scm.branches,
                                    extensions: scm.extensions + [
                                            [
                                            $class: 'SubmoduleOption',
                                            disableSubmodules: false,
                                            recursiveSubmodules: true,
                                            parentCredentials: true
                                            ]
                                    ]
                            ])

                            branchName = vcs.GIT_BRANCH
                            commitId = "${vcs.GIT_COMMIT}"[0..6]
                            repoName = vcs.GIT_URL.tokenize( '/' ).last().tokenize( '.' ).first()

                            release_version = branchName.replaceAll("[^\\d\\.]", "");
                            if (release_version.length() > 0 || branchName.contains('release')) {
                                version = release_version + "." + buildNumber
                            } else {
                                version = "0.0.${buildNumber}"
                            }
                        }

                        // clean
                        sh 'make distclean'
                        sh 'touch src/version/version.c'

                        // build tests
                        sh 'make test AVS_VERSION=' + version
                        // run tests
                        sh './ztest'

                        sh 'make dist_clean'
                        sh 'make zcall AVS_VERSION=' + version
                        sh '''#!/bin/bash
                            . ./scripts/android_devenv.sh && make dist_linux dist_android AVS_VERSION=''' + version + '  BUILDVERSION=' + version + '''
                        '''
                        sh 'rm -rf ./build/artifacts'
                        sh 'mkdir -p ./build/artifacts'
                        sh 'cp ./build/dist/linux/avscore.tar.bz2 ./build/artifacts/avs.linux.' + version + '.tar.bz2'
                        sh 'zip -9j ./build/artifacts/avs.android.' + version + '.zip ./build/dist/android/avs.aar'
                        sh 'zip -9j ./build/artifacts/zcall_linux_' + version + '.zip ./zcall'
                        sh 'if [ -e ./build/dist/android/debug/ ]; then cd ./build/dist/android/debug; zip -9r ./../../../artifacts/avs.android.' + version + '.debug.zip *; cd -; fi'

                        archiveArtifacts artifacts: 'build/artifacts/*', followSymlinks: false
                    }
                }
                stage('macOS') {
                    agent {
                        label 'built-in'
                    }
                    steps {
                        script {
                            def vcs = checkout([
                                    $class: 'GitSCM',
                                    changelog: true,
                                    userRemoteConfigs: scm.userRemoteConfigs,
                                    branches: scm.branches,
                                    extensions: scm.extensions + [
                                            [
                                            $class: 'SubmoduleOption',
                                            disableSubmodules: false,
                                            recursiveSubmodules: true,
                                            parentCredentials: true
                                            ]
                                    ]
                            ])

                            branchName = vcs.GIT_BRANCH
                            commitId = "${vcs.GIT_COMMIT}"[0..6]
                            repoName = vcs.GIT_URL.tokenize( '/' ).last().tokenize( '.' ).first()

                            release_version = branchName.replaceAll("[^\\d\\.]", "");
                            if (release_version.length() > 0 || branchName.contains('release')) {
                                version = release_version + "." + buildNumber
                            } else {
                                version = "0.0.${buildNumber}"
                            }
                        }

                        // clean
                        sh 'make distclean'
                        sh 'touch src/version/version.c'

                        // build tests
                        sh 'make test AVS_VERSION=' + version
                        // run tests
                        sh './ztest'

                        // build
                        sh 'make dist_clean'
                        sh 'make zcall AVS_VERSION=' + version
                        sh '''#!/bin/bash
                            . ./scripts/wasm_devenv.sh && make dist_osx dist_ios dist_wasm AVS_VERSION=''' + version + '  BUILDVERSION=' + version + '''
                        '''

                        sh 'rm -rf ./build/artifacts'
                        sh 'mkdir -p ./build/artifacts'
                        sh 'cp ./build/dist/osx/avs.framework.zip ./build/artifacts/avs.framework.osx.' + version + '.zip'
                        sh 'cp ./build/dist/ios/avs.xcframework.zip ./build/artifacts/avs.xcframework.zip'
                        sh 'zip -9j ./build/artifacts/zcall_osx_' + version + '.zip ./zcall'
                        sh 'mkdir -p ./osx'
                        sh 'cp ./build/dist/osx/avscore.tar.bz2 ./osx'
                        sh 'cp ./build/dist/wasm/wireapp-avs-' + version + '.tgz ./build/artifacts/'

                        archiveArtifacts artifacts: 'build/artifacts/*', followSymlinks: false
                    }
                }
            }
        }
        stage('Prepare changelog') {
            steps {
                script {
                    currentBuild.changeSets.each { set ->
                        set.items.each { entry ->
                            changelog += '- ' + entry.msg + '\n'
                        }
                    }
                }
                echo("Changelog:")
                echo(changelog)
            }
        }
        stage('Tag + Create Github release') {
            agent {
                label "linuxbuild"
            }
            when {
                anyOf {
                    expression { return "${branchName}".contains('release') || "${branchName}".contains('main') }
                }
            }

            steps {
                echo "Tag as ${version}"
                withCredentials([sshUserPrivateKey(credentialsId: 'wire-avs', keyFileVariable: 'sshPrivateKeyPath')]) {
                    sh(
                        script: """
                            cd "${env.WORKSPACE}"

                            git tag ${version}

                            git \
                                -c core.sshCommand='ssh -i ${sshPrivateKeyPath}' \
                                push \
                                origin ${version}
                        """
                    )
                }
                echo 'Creating release on Github'
                // Unfortunately it is not allowed to access github API with a deploy key so we have to rely on
                // a user token to upload via python github package
                withCredentials([ string( credentialsId: 'github-repo-user', variable: 'repoUser' ),
                    string( credentialsId: 'github-repo-access', variable: 'accessToken' ) ]) {
                    // NOTE: creating an empty stub directory just to create the release
                    sh(
                        script: """
                            cd "${env.WORKSPACE}"
                            GITHUB_USER=${repoUser} \
                            GITHUB_TOKEN=${accessToken} \
			    python3 ./scripts/release-on-github.py \
                                ${repoName} \
                                \$(mktemp -d) \
                                ${version} \
                                "${changelog}"
                        """
                    )
                }
            }
        }
        stage('Upload to Github') {
            when {
                anyOf {
                    expression { return "${branchName}".contains('release') || "${branchName}".contains('main') }
                }
            }
            matrix {
                axes {
                    axis {
                        name 'AGENT'
                        values 'built-in', 'linuxbuild'
                    }
                }
                agent {
                    label "${AGENT}"
                }

                stages {
                    stage( 'Uploading release artifacts' ) {
                        steps {
                            withCredentials([ string( credentialsId: 'github-repo-user', variable: 'repoUser' ),
                                string( credentialsId: 'github-repo-access', variable: 'accessToken' ) ]) {
                                sh(
                                    script: """
                                        GITHUB_USER=${repoUser} \
                                        GITHUB_TOKEN=${accessToken} \
                                        python3 ./scripts/release-on-github.py \
                                            ${repoName} \
                                            ./build/artifacts \
                                            ${version} \
                                            "${changelog}"
                                    """
                                )
                            }
                        }
                    }
                }
            }
        }
        stage('Publish to sonatype') {
            when {
                anyOf {
                    expression { return "${branchName}".contains('release') }
                }
            }
            agent {
                label 'linuxbuild'
            }
            environment {
                PATH = "/opt/homebrew/bin:/Applications/Xcode.app/Contents/Developer/usr/bin:/Users/jenkins/.cargo/bin:/usr/local/bin:${ env.PATH }"
            }
            steps {
                script {
                    echo '### Sign and upload to sonatype'
                    withCredentials([ usernamePassword( credentialsId: 'android-sonatype-nexus', usernameVariable: 'SONATYPE_USERNAME', passwordVariable: 'SONATYPE_PASSWORD' ),
                                        file(credentialsId: 'D599C1AA126762B1.asc', variable: 'PGP_PRIVATE_KEY_FILE'),
                                        string(credentialsId: 'PGP_PASSPHRASE', variable: 'PGP_PASSPHRASE') ]) {
                        try {
                            withMaven(maven: 'M3') {
                                sh(
                                    script: """
                                        mkdir -p .gpghome
                                        chmod 700 .gpghome
                                        gpg --batch \
                                            --homedir .gpghome \
                                            --quiet \
                                            --import "${PGP_PRIVATE_KEY_FILE}"

                                        touch local.properties

                                        version=$version ./gradlew publishToSonatype closeAndReleaseSonatypeStagingRepository \
                                            -Psigning.keyId=126762B1 \
                                            -Psigning.password=$PGP_PASSPHRASE \
                                            -Psigning.secretKeyRingFile=.gpphome/seckeyring.gpg
                                    """
                                )
                            }
                        } finally {
                            // Cleanup gpg dot files in any case
                            sh(
                                script: """
                                    rm -rf .gpghome
                                """
                            )
                        }
                    }
                }
            }
        }
        stage('Publish to ios github repo') {
            when {
                anyOf {
                    expression { return "${branchName}".contains('release') }
                }
            }
            agent {
                label 'built-in'
            }
            steps {
                withCredentials([ string( credentialsId: 'ios-github', variable: 'accessToken' ) ]) {
                    sh """
                        GITHUB_TOKEN=${ accessToken } \
                        python3 ./scripts/upload-ios.py \
                            ./build/artifacts/avs.xcframework.zip \
                            ${version} \
                            appstore
                    """
                }
            }
        }
        stage('Publish to npm') {
            when {
                anyOf {
                    expression { return "${branchName}".contains('release') }
                }
            }
            agent {
                label 'linuxbuild'
            }
            steps {
                // NOTE: the script upload-wasm.sh supports non-release branches, but in the past
                //       it still was only invoked on release branches
                nodejs("18.12.1") {
                    withCredentials([ string( credentialsId: 'npmtoken', variable: 'accessToken' ) ]) {
                        sh """
                            # NOTE: upload-wasm.sh assumes a certain current working directory
                            NPM_TOKEN=${accessToken} \
                            ${env.WORKSPACE}/scripts/upload-wasm.sh avs-release-${release_version}
                        """
                    }
                }
            }
        }
    }

    post {
        always {
            node('linuxbuild') {
                script {
                    sh 'docker container prune -f && docker volume prune -f && docker image prune -f'
                }
            }
        }

        success {
            node( 'built-in' ) {
                withCredentials([ string( credentialsId: 'wire-jenkinsbot', variable: 'jenkinsbot_secret' ) ]) {
                    wireSend secret: "$jenkinsbot_secret", message: "✅ ${JOB_NAME} #${BUILD_ID} succeeded\n**Changelog:**\n${changelog}\n${BUILD_URL}console\nhttps://github.com/wireapp/wire-avs/commit/${commitId}"
                }
            }
        }

        failure {
            node( 'built-in' ) {
                withCredentials([ string( credentialsId: 'wire-jenkinsbot', variable: 'jenkinsbot_secret' ) ]) {
                    wireSend secret: "$jenkinsbot_secret", message: "❌ ${JOB_NAME} #${BUILD_ID} failed\n${BUILD_URL}console\nhttps://github.com/wireapp/wire-avs/commit/${commitId}"
                }
            }
        }
    }
}
